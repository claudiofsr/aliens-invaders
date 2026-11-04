//! Entity-layer golden parity (RUST.md slice 4b): every scenario of the
//! tools/dump_vectors.cc ENTITY_BLOCK is replayed against the committed
//! corpus and asserted BIT-EXACT, with one documented exception: the angle
//! columns. Their last bit comes from atan2f, and atan2f's last bit depends on
//! the glibc build that replays it: the same committed corpus passes bit-exact
//! on the recording machine and diverges on any other machine: per-frame
//! atan2f noise integrates through the easing into offsets of hundreds of
//! ulps (measured worst: 672 ~= 0.13 deg). Angles are
//! cosmetic -- no dynamics ever reads angle_ back (hull attitudes feed
//! rendering; the single dynamics path through a deflection angle is
//! FastRound-quantized) -- so the tight budget below cannot mask a state
//! divergence. Everything else admits no tolerance: a diverged dynamic float
//! cascades through RNG-gated behavior and pool ordering, so any other
//! mismatch is a real porting bug. The corpus pins the toolchain contract for
//! arithmetic (IEEE-754 + sqrt, platform-independent) and the libstdc++
//! Lemire downscaling since GCC 11 -- see src/dist.rs.
//!
//! Scenario inputs that the corpus does NOT carry (manager seeds, the three
//! bullet adds, the convoy table) are hardcoded here exactly as the dumper
//! hardcodes them; every hardcoded value that the corpus echoes back (convoy
//! rows, path rows, setup rows) is asserted, so a contract change on either
//! side fails loudly.

use kernel::{
    alien::Alien,
    bonus::{BonusManager, BonusType},
    bullets::{BulletsManager, SEEKER_ENGINE_COLOR},
    explosions::ExplosionsManager,
    fleet::{ConvoySpec, Fleet},
    formation,
    game_object::GameObject,
    highscore::{content_hash, HighScoreTable, HighScores},
    level_data,
    mathx::Coord,
    path::{self, Vec2f},
    player::{Player, PLAYER_BOTTOM_MARGIN_PX},
    score,
    session::{
        cinematic_frames, details_osd_frames, overlay_alpha_at, phase_transition_frames,
        EntitySession, DESTRUCTION_CINEMATIC_SECONDS, DESTRUCTION_SKIP_DIVISOR,
        GAME_OVER_SECONDS, NOVA_CINEMATIC_SECONDS, NOVA_SKIP_DIVISOR,
    },
    sink::{EventSink, PresentationEvent},
    stage_catalog,
    stars::{self, StarsFields, STARFIELD_SEED, WARP_DURATION_FRAMES},
    textures::{dims, TextureId, HEADLESS_DIMS},
    trajectory::{Trajectory, ViewportMetrics},
};
use std::collections::VecDeque;
use std::path::PathBuf;

// ---- Corpus plumbing -------------------------------------------------------

fn load_rows() -> Vec<Vec<String>> {
    let path =
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../golden/vectors-cpp.txt");
    let text = std::fs::read_to_string(&path).unwrap_or_else(|e| {
        panic!(
            "golden vectors missing ({}): {}\nGenerate them first:\n  \
             make dump_vectors && ./build/dump_vectors > golden/vectors-cpp.txt",
            path.display(),
            e
        )
    });
    text.lines()
        .filter(|l| !l.starts_with('#') && !l.trim().is_empty())
        .map(|l| l.split_whitespace().map(str::to_string).collect())
        .collect()
}

/// File-order row queue for one corpus section (prefix match).
struct Rows {
    q: VecDeque<Vec<String>>,
}

fn section(rows: &[Vec<String>], prefix: &str) -> Rows {
    Rows {
        q: rows.iter().filter(|r| r[0].starts_with(prefix)).cloned().collect(),
    }
}

impl Rows {
    fn next(&mut self, tag: &str) -> Vec<String> {
        let row = self
            .q
            .pop_front()
            .unwrap_or_else(|| panic!("corpus exhausted: expected {tag}"));
        assert_eq!(row[0], tag, "expected row {tag}");
        row
    }

    fn peek_is(&self, tag: &str) -> bool {
        self.q.front().is_some_and(|r| r[0] == tag)
    }

    fn done(&self, ctx: &str) {
        assert!(
            self.q.is_empty(),
            "{ctx}: {} unconsumed corpus rows (next: {:?})",
            self.q.len(),
            self.q.front().map(|r| r.join(" "))
        );
    }
}

fn hex32(tok: &str) -> u32 {
    u32::from_str_radix(tok, 16).unwrap_or_else(|_| panic!("bad hex32 token: {tok}"))
}

fn hb(tok: &str) -> f32 {
    f32::from_bits(hex32(tok))
}

/// Budget for the transcendental-derived corpus columns: every `angle` field
/// (hull attitudes from atan2f via AdvanceTurnDeg, and the bomb render and
/// deflection angles built from them). The last bit of atan2f belongs to the
/// glibc build that replays it, and angle_ integrates that noise through the
/// per-frame easing, converging to a stationary offset: the measured worst
/// case is 672 ulps across the full fleet and kamikaze replays (<= 0.13 deg)
/// against a glibc older than the recorder's. The budget is ~6x that worst
/// case; a logic bug (swapped operands, stale frame, wrong easing) misses the
/// corpus by degrees -- four to six orders of magnitude more. To recalibrate
/// after a glibc change: set the budget to i64::MAX, print `ulps` instead of
/// asserting, replay every scenario, and size the budget from the observed
/// maximum. If the drift ever extends to other libm-derived columns
/// (sin/cos-fed velocities or arc positions), the same mechanism applies --
/// extend it then, never before the corpus proves it.
const ANGLE_ULP_BUDGET: i64 = 4096;

/// Monotonic ordering of IEEE-754 bit patterns: for finite floats of the same
/// sign the integer distance between ordered values is the exact ulp count
/// (distances across zero conservatively count through the zero pair).
fn ordered_bits(bits: u32) -> i64 {
    let as_int = bits as i32;
    if as_int < 0 {
        (i32::MIN - as_int) as i64
    } else {
        i64::from(as_int)
    }
}

fn assert_angle_bits(corpus_tok: &str, rust_bits: u32, ctx: &str) {
    let corpus_bits = hex32(corpus_tok);
    let ulps = (ordered_bits(corpus_bits) - ordered_bits(rust_bits)).abs();
    assert!(
        ulps <= ANGLE_ULP_BUDGET,
        "{ctx}: angle {corpus_bits:#010x} (corpus) vs {rust_bits:#010x} (rust), {ulps} ulps > {ANGLE_ULP_BUDGET}"
    );
}

fn i(row: &[String], idx: usize) -> i32 {
    row[idx].parse().unwrap_or_else(|_| panic!("bad int token: {}", row[idx]))
}

fn u(row: &[String], idx: usize) -> u64 {
    row[idx].parse().unwrap_or_else(|_| panic!("bad u64 token: {}", row[idx]))
}

fn sz(row: &[String], idx: usize) -> usize {
    row[idx].parse().unwrap_or_else(|_| panic!("bad size token: {}", row[idx]))
}

/// The entity scenarios all run in the explicit scale-1 world the dumper sets
/// up front (1280x720 viewport, FormationGrid::Recompute(1.0)).
const METRICS: ViewportMetrics =
    ViewportMetrics { win_w: 1280, win_h: 720, scale: 1.0 };

fn parse_sink(row: &[String]) -> PresentationEvent {
    match row[2].as_str() {
        "text" => PresentationEvent::Text {
            x: i(row, 3),
            y: i(row, 4),
            r: i(row, 5) as u8,
            g: i(row, 6) as u8,
            b: i(row, 7) as u8,
            font_size: hb(&row[8]),
            ttl: i(row, 9),
            text: row[10..].join(" "),
        },
        "flash" => PresentationEvent::Flash {
            r: i(row, 3) as u8,
            g: i(row, 4) as u8,
            b: i(row, 5) as u8,
            frames: i(row, 6),
        },
        "trauma" => PresentationEvent::Trauma(hb(&row[3])),
        "sfx" => PresentationEvent::Sfx { id: i(row, 3), pan: hb(&row[4]) },
        other => panic!("unknown sink kind: {other}"),
    }
}

/// Compares the events accumulated since the last drain against the frame's
/// sink rows (row count and order are part of the contract).
fn drain_sink(sink: &mut EventSink, sec: &mut Rows, tag: &str, frame: i32) {
    for ev in sink.drain() {
        let row = sec.next(tag);
        assert_eq!(i(&row, 1), frame, "{tag}: frame stamp");
        assert_eq!(parse_sink(&row), ev, "{tag} f{frame}: event mismatch");
    }
    assert!(
        !sec.peek_is(tag),
        "{tag}: corpus rows remain for f{frame} but the sink drained empty"
    );
}

/// `drain_sink` variant for SPARSE sink streams (the sampled ent.fb frame
/// rows and the match-flow replays): the corpus carries sink rows on frames
/// that have no frame row (the 40-frame shot heartbeat, an isolated bonus
/// pickup), so a sink row queued for a FUTURE frame is legitimate. Only a
/// row stamped at or before the current frame is a divergence (a missed
/// event); an extra event fails through the stamp and value asserts of the
/// row it consumes out of turn.
fn drain_sink_fb(sink: &mut EventSink, sec: &mut Rows, tag: &str, frame: i32) {
    for ev in sink.drain() {
        let row = sec.next(tag);
        assert_eq!(i(&row, 1), frame, "{tag}: frame stamp");
        assert_eq!(parse_sink(&row), ev, "{tag} f{frame}: event mismatch");
    }
    if let Some(head) = sec.q.front() {
        if head[0] == tag {
            assert!(
                i(head, 1) > frame,
                "{tag}: stale corpus rows at f{frame} (sink drained empty)"
            );
        }
    }
}

// ---- ent.pix: headless dimension table -------------------------------------

#[test]
fn pix_parity() {
    let rows = load_rows();
    let mut sec = section(&rows, "ent.pix");
    for id in 0..TextureId::Count as i32 {
        let row = sec.next("ent.pix");
        assert_eq!(i(&row, 1), id);
        let (w, h) = HEADLESS_DIMS[id as usize];
        assert_eq!(i(&row, 2), w, "pix {id} width");
        assert_eq!(i(&row, 3), h, "pix {id} height");
        assert_eq!(dims(kernel::textures::from_id(id)), (w, h));
    }
    sec.done("ent.pix");
}

// ---- ent.bl: projectile pool micro-replay ----------------------------------

#[test]
fn bullets_parity() {
    let rows = load_rows();
    let mut sec = section(&rows, "ent.bl.");
    let mut bl = BulletsManager::new(0xB011E75);
    bl.set_metrics(METRICS);

    // The three documented scenario adds (textures are not carried by the
    // rows; this call set IS the corpus contract -- see DumpEntityBullets).
    bl.add(
        TextureId::Bullet,
        Coord::new(300, 600),
        Coord::new(0, -8),
        false,
        false,
        0,
        0.0,
        false,
        SEEKER_ENGINE_COLOR,
    );
    bl.add(
        TextureId::Bomb,
        Coord::new(200, 100),
        Coord::new(3, 5),
        true,
        false,
        0,
        0.0,
        false,
        SEEKER_ENGINE_COLOR,
    );
    bl.add(
        TextureId::Bomb,
        Coord::new(900, 80),
        Coord::new(-2, 4),
        false,
        true,
        300,
        17.5,
        false,
        SEEKER_ENGINE_COLOR,
    );

    for a in 0..3usize {
        let row = sec.next("ent.bl.add");
        assert_eq!(sz(&row, 1), a);
        let b = bl.get_active(a);
        assert_eq!(i(&row, 2), b.pos.x, "add {a} x");
        assert_eq!(i(&row, 3), b.pos.y, "add {a} y");
        assert_eq!(i(&row, 4), b.speed.x, "add {a} speed.x");
        assert_eq!(i(&row, 5), b.speed.y, "add {a} speed.y");
        assert_eq!(i(&row, 6), i32::from(b.relativistic), "add {a} relativistic");
        assert_eq!(i(&row, 7), i32::from(b.is_turning_bomb), "add {a} turning");
        assert_eq!(i(&row, 8), b.turn_trigger_y, "add {a} trigger_y");
        assert_angle_bits(&row[9], b.deflection_angle_deg.to_bits(), &format!("add {a} defl"));
    }

    // Synthetic interception target: headless player dimensions at (300,320).
    let (pw, ph) = dims(TextureId::Player);
    let target = GameObject::new(Coord::new(300, 320), pw, ph);

    for f in 0..110i32 {
        bl.advance(Coord::new(640, 660));

        let row = sec.next("ent.bl.frame");
        assert_eq!(i(&row, 1), f);
        let (min_y, max_y) = bl.y_bounds();
        assert_eq!(i(&row, 2), bl.nb(), "f{f} nb");
        assert_eq!(i(&row, 3), bl.active_exhaust_count(), "f{f} exhaust nb");
        assert_eq!(i(&row, 4), min_y, "f{f} min_y");
        assert_eq!(i(&row, 5), max_y, "f{f} max_y");

        for a in 0..bl.nb() as usize {
            let row = sec.next("ent.bl.slot");
            let b = bl.get_active(a);
            assert_eq!(i(&row, 1), f);
            assert_eq!(sz(&row, 2), a);
            assert_eq!(i(&row, 3), b.pos.x, "f{f} slot{a} x");
            assert_eq!(i(&row, 4), b.pos.y, "f{f} slot{a} y");
            assert_eq!(hex32(&row[5]), b.fx.to_bits(), "f{f} slot{a} fx");
            assert_eq!(hex32(&row[6]), b.fy.to_bits(), "f{f} slot{a} fy");
            assert_eq!(i(&row, 7), b.speed.x, "f{f} slot{a} speed.x");
            assert_eq!(i(&row, 8), b.speed.y, "f{f} slot{a} speed.y");
            assert_eq!(i(&row, 9), i32::from(b.relativistic), "f{f} slot{a} rel");
            assert_eq!(i(&row, 10), i32::from(b.radioactive), "f{f} slot{a} radio");
            assert_eq!(i(&row, 11), b.wave_phase, "f{f} slot{a} wave_phase");
            assert_eq!(i(&row, 12), i32::from(b.is_turning_bomb), "f{f} slot{a} turning");
            assert_eq!(i(&row, 13), i32::from(b.turn_completed), "f{f} slot{a} done");
            assert_eq!(i(&row, 14), b.turn_timer, "f{f} slot{a} timer");
            assert_angle_bits(&row[15], b.bomb_render_angle.to_bits(), &format!("f{f} slot{a} render"));
            assert_angle_bits(&row[16], b.deflection_angle_deg.to_bits(), &format!("f{f} slot{a} defl"));
            assert_eq!(hex32(&row[17]), b.speed_magnitude.to_bits(), "f{f} slot{a} mag");
        }

        if f % 10 == 0 {
            let n = (bl.active_exhaust_count() as usize).min(8);
            for a in 0..n {
                let row = sec.next("ent.bl.exhaust");
                let e = bl.get_active_exhaust(a);
                assert_eq!(i(&row, 1), f);
                assert_eq!(sz(&row, 2), a);
                assert_eq!(hex32(&row[3]), e.pos.x.to_bits(), "f{f} exhaust{a} px");
                assert_eq!(hex32(&row[4]), e.pos.y.to_bits(), "f{f} exhaust{a} py");
                assert_eq!(hex32(&row[5]), e.vel.x.to_bits(), "f{f} exhaust{a} vx");
                assert_eq!(hex32(&row[6]), e.vel.y.to_bits(), "f{f} exhaust{a} vy");
                assert_eq!(i(&row, 7), e.life, "f{f} exhaust{a} life");
            }
        }

        while sec.peek_is("ent.bl.radius") {
            let row = sec.next("ent.bl.radius");
            assert_eq!(i(&row, 1), f);
            let probe = Coord::new(i(&row, 2), i(&row, 3));
            let got = bl.has_projectile_within_radius(probe, hb(&row[4]));
            assert_eq!(i(&row, 5), i32::from(got), "f{f} radius probe");
        }
        if sec.peek_is("ent.bl.collide") {
            let row = sec.next("ent.bl.collide");
            assert_eq!(i(&row, 1), f);
            assert_eq!(i(&row, 2), target.pos.x);
            assert_eq!(i(&row, 3), target.pos.y);
            let hits = bl.do_collisions(&target, 2);
            assert_eq!(i(&row, 4), hits, "f{f} collide hits");
        }
        while sec.peek_is("ent.bl.near") {
            let row = sec.next("ent.bl.near");
            assert_eq!(i(&row, 1), f);
            let got = bl.has_bomb_near(Coord::new(i(&row, 2), i(&row, 3)), i(&row, 4));
            assert_eq!(i(&row, 5), i32::from(got), "f{f} bomb-near probe");
        }
        while sec.peek_is("ent.bl.trap") {
            let row = sec.next("ent.bl.trap");
            let spawn = Coord::new(i(&row, 2), i(&row, 3));
            let speed = Coord::new(i(&row, 4), i(&row, 5));
            let got = bl.would_trap_player(
                spawn,
                speed,
                i(&row, 6),
                i(&row, 7),
                i(&row, 8),
                METRICS.win_w,
            );
            assert_eq!(i(&row, 9), i32::from(got), "trap case {}", row[1]);
        }
    }
    sec.done("ent.bl");
}

// ---- ent.ex / ent.ex2: explosion pool --------------------------------------

fn sample_particles(
    ex: &ExplosionsManager,
    sec: &mut Rows,
    frame_tag: &str,
    p_tag: &str,
    f: i32,
    max_samples: usize,
) {
    let row = sec.next(frame_tag);
    assert_eq!(i(&row, 1), f);
    assert_eq!(sz(&row, 2), ex.active_count(), "{frame_tag} f{f} active");
    let n = ex.active_count().min(max_samples);
    for a in 0..n {
        let row = sec.next(p_tag);
        let p = ex.get_active_particle(a);
        assert_eq!(i(&row, 1), f);
        assert_eq!(sz(&row, 2), a);
        assert_eq!(hex32(&row[3]), p.pos.x.to_bits(), "{p_tag} f{f} p{a} x");
        assert_eq!(hex32(&row[4]), p.pos.y.to_bits(), "{p_tag} f{f} p{a} y");
        assert_eq!(hex32(&row[5]), p.vel.x.to_bits(), "{p_tag} f{f} p{a} vx");
        assert_eq!(hex32(&row[6]), p.vel.y.to_bits(), "{p_tag} f{f} p{a} vy");
        assert_eq!(i(&row, 7), p.r as i32, "{p_tag} f{f} p{a} r");
        assert_eq!(i(&row, 8), p.g as i32, "{p_tag} f{f} p{a} g");
        assert_eq!(i(&row, 9), p.b as i32, "{p_tag} f{f} p{a} b");
        assert_eq!(i(&row, 10), p.a as i32, "{p_tag} f{f} p{a} a");
        assert_eq!(i(&row, 11), p.life as i32, "{p_tag} f{f} p{a} life");
        assert_eq!(i(&row, 12), p.max_life as i32, "{p_tag} f{f} p{a} max_life");
    }
}

#[test]
fn explosions_parity() {
    let rows = load_rows();
    let mut sec = section(&rows, "ent.ex");
    let grid = formation::recompute(1.0);

    let row = sec.next("ent.ex.seed");
    let seed = u(&row, 1) as u32;
    let mut ex = ExplosionsManager::new(2, grid.width);
    ex.seed_random(seed);
    ex.set_metrics(1.0);

    for a in 0..3usize {
        let row = sec.next("ent.ex.add");
        assert_eq!(sz(&row, 1), a);
        let pos = Coord::new(i(&row, 2), i(&row, 3));
        let vel = Coord::new(i(&row, 4), i(&row, 5));
        let (r, g, b) = (i(&row, 6) as u8, i(&row, 7) as u8, i(&row, 8) as u8);
        ex.add(pos, vel, r, g, b, i(&row, 9));
    }

    for f in 0..70i32 {
        ex.advance();
        if f % 5 == 0 {
            sample_particles(&ex, &mut sec, "ent.ex.frame", "ent.ex.p", f, 6);
        }
    }
    ex.clear();
    let row = sec.next("ent.ex.clear");
    assert_eq!(sz(&row, 1), ex.active_count(), "post-clear active");

    ex.trigger_screen_wide_nova(Coord::new(640, 640), METRICS.win_w, METRICS.win_h);
    let row = sec.next("ent.ex.nova");
    assert_eq!(sz(&row, 1), ex.active_count(), "post-nova active");

    for f in 0..40i32 {
        ex.advance();
        if f % 10 == 0 {
            sample_particles(&ex, &mut sec, "ent.ex2.frame", "ent.ex2.p", f, 4);
        }
    }
    sec.done("ent.ex");
}

// ---- ent.wp: Alien14 relativistic warp --------------------------------------

#[test]
fn warp_parity() {
    let rows = load_rows();
    let mut sec = section(&rows, "ent.wp");
    let arrival = path::flight_path_loop(120.0, -60.0, 2);

    // Birth-roll seed search: the first seed whose Alien14 is born
    // warp-capable. The search is deterministic on both sides of the port;
    // the corpus row pins the outcome.
    let mut chosen = 0u32;
    let mut ship_opt: Option<Alien> = None;
    for s in 1u32..=4096 {
        let t = Trajectory::new(&arrival, false, (300, 82), METRICS, 3, 1, s, 1, 16);
        let candidate = Alien::new(t, 6.0, TextureId::Alien14);
        if candidate.warp_evasions_remaining() > 0 {
            chosen = s;
            ship_opt = Some(candidate);
            break;
        }
    }
    let mut ship = ship_opt.expect("no warp-capable Alien14 seed found (1..4096)");
    let row = sec.next("ent.wp.seed");
    assert_eq!(u(&row, 1) as u32, chosen, "warp seed search diverged");

    let mut sink = EventSink::default();
    let ct = ship.trajectory().cruise_target();
    ship.force_cruise(Coord::new(ct.0, ct.1));

    // Occupancy literal from the corpus contract: row 7 partially full,
    // row 9 striped, every other row free.
    let occupancy: [u16; 12] = [0, 0, 0, 0, 0, 0, 0, 0x00FF, 0, 0x0F0F, 0, 0];

    for step in 0..3i32 {
        let row = sec.next("ent.wp.step");
        assert_eq!(i(&row, 1), step);

        let row = sec.next("ent.wp.can");
        assert_eq!(i(&row, 1), step);
        assert_eq!(i(&row, 2), i32::from(ship.can_trigger_warp()), "step {step} can");

        let row = sec.next("ent.wp.occ");
        assert_eq!(i(&row, 1), step);
        for k in 0..12usize {
            assert_eq!(u(&row, 2 + k) as u16, occupancy[k], "step {step} occ row {k}");
        }
        assert_eq!(i(&row, 14), 640, "step {step} player_x");
        assert_eq!(i(&row, 15), 660, "step {step} player_y");
        assert_eq!(i(&row, 16), 12, "step {step} rows");

        ship.trigger_spacetime_warp(&occupancy, 640, 660, 12, &mut sink);

        let frames = if step == 2 { 30 } else { 150 };
        let sink_tag = format!("ent.wp{step}.sink");
        for f in 0..frames {
            ship.step();
            let row = sec.next("ent.wp.frame");
            assert_eq!(i(&row, 1), step);
            assert_eq!(i(&row, 2), f);
            assert_eq!(i(&row, 3), ship.stage() as i32, "step {step} f{f} stage");
            assert_eq!(i(&row, 4), ship.position().x, "step {step} f{f} x");
            assert_eq!(i(&row, 5), ship.position().y, "step {step} f{f} y");
            assert_angle_bits(&row[6], ship.angle().to_bits(), &format!("step {step} f{f} angle"));
            assert_eq!(i(&row, 7), ship.warp_evasions_remaining(), "step {step} f{f} evasions");
            assert_eq!(i(&row, 8), i32::from(ship.is_warping()), "step {step} f{f} warping");
            drain_sink(&mut sink, &mut sec, &sink_tag, f);
        }
    }
    sec.done("ent.wp");
}

// ---- ent.dc: Alien4 alpha decay ---------------------------------------------

#[test]
fn decay_parity() {
    let rows = load_rows();
    let mut sec = section(&rows, "ent.dc.");
    let arrival = path::flight_path_loop(120.0, -60.0, 2);

    // Birth-roll seed search: two electron-phase draws precede the
    // electrosphere probability roll, so the draw ORDER is part of the
    // contract this search pins down.
    let mut chosen = 0u32;
    let mut ship_opt: Option<Alien> = None;
    for s in 1u32..=4096 {
        let t = Trajectory::new(&arrival, false, (300, 82), METRICS, 4, 2, s, 1, 8);
        let candidate = Alien::new(t, 6.0, TextureId::Alien4);
        if candidate.can_trigger_alpha_decay() {
            chosen = s;
            ship_opt = Some(candidate);
            break;
        }
    }
    let mut ship = ship_opt.expect("no electrosphere Alien4 seed found (1..4096)");
    let row = sec.next("ent.dc.seed");
    assert_eq!(u(&row, 1) as u32, chosen, "decay seed search diverged");

    let mut sink = EventSink::default();
    let _ = &mut sink; // decay is silent; the sink exists for contract parity
    let ct = ship.trajectory().cruise_target();
    ship.force_cruise(Coord::new(ct.0, ct.1));

    // Three phases: baseline cruise, the decay trigger (scale 0.5 and the
    // x1.3 cruise speed show up in the frame deltas) and a second trigger
    // that must be a silent no-op (health is now 1).
    let phase_frames = [30i32, 60, 30];
    for phase in 0..3i32 {
        if phase > 0 {
            ship.trigger_alpha_decay();
            let row = sec.next("ent.dc.trigger");
            assert_eq!(i(&row, 1), phase);
            assert_eq!(hex32(&row[2]), ship.scale().to_bits(), "phase {phase} scale");
            assert_eq!(
                i(&row, 3),
                i32::from(ship.can_trigger_alpha_decay()),
                "phase {phase} can_decay"
            );
        }
        for f in 0..phase_frames[phase as usize] {
            ship.step();
            let row = sec.next("ent.dc.frame");
            assert_eq!(i(&row, 1), phase);
            assert_eq!(i(&row, 2), f);
            assert_eq!(i(&row, 3), ship.stage() as i32, "phase {phase} f{f} stage");
            assert_eq!(i(&row, 4), ship.position().x, "phase {phase} f{f} x");
            assert_eq!(i(&row, 5), ship.position().y, "phase {phase} f{f} y");
            assert_angle_bits(&row[6], ship.angle().to_bits(), &format!("phase {phase} f{f} angle"));
            assert_eq!(hex32(&row[7]), ship.scale().to_bits(), "phase {phase} f{f} scale");
            assert_eq!(
                i(&row, 8),
                i32::from(ship.can_trigger_alpha_decay()),
                "phase {phase} f{f} can_decay"
            );
        }
    }
    assert!(sink.drain().is_empty(), "decay must stay silent");
    sec.done("ent.dc");
}

// ---- ent.kz: kamikaze guided dive -------------------------------------------

#[test]
fn kamikaze_parity() {
    let rows = load_rows();
    let mut sec = section(&rows, "ent.kz.");
    let arrival = path::flight_path_loop(120.0, -60.0, 2);

    let t = Trajectory::new(&arrival, false, (300, 82), METRICS, 2, 4, 4242, 3, 8);
    let mut ship = Alien::new(t, 6.0, TextureId::Alien7);
    ship.set_kamikaze(true);
    let ct = ship.trajectory().cruise_target();
    ship.force_cruise(Coord::new(ct.0, ct.1));

    let row = sec.next("ent.kz.seed");
    assert_eq!(u(&row, 1) as u32, 4242);
    let row = sec.next("ent.kz.init");
    assert_eq!(i(&row, 1), ship.position().x, "init x");
    assert_eq!(i(&row, 2), ship.position().y, "init y");
    let target = Coord::new(640, 640);
    let row = sec.next("ent.kz.target");
    assert_eq!(i(&row, 1), target.x);
    assert_eq!(i(&row, 2), target.y);

    ship.build_kamikaze_dive(target);
    // 420 frames cover the whole cycle: guided dive -> off-screen wrap ->
    // re-dock (joining) -> cruise.
    for f in 0..420i32 {
        ship.step();
        let row = sec.next("ent.kz.frame");
        assert_eq!(i(&row, 1), f);
        assert_eq!(i(&row, 2), ship.stage() as i32, "f{f} stage");
        assert_eq!(i(&row, 3), ship.position().x, "f{f} x");
        assert_eq!(i(&row, 4), ship.position().y, "f{f} y");
        assert_angle_bits(&row[5], ship.angle().to_bits(), &format!("f{f} angle"));
        assert_eq!(i(&row, 6), i32::from(ship.is_kamikaze()), "f{f} kamikaze");
    }
    sec.done("ent.kz");
}

// ---- ent.pl: player economy --------------------------------------------------

/// Production tick order restricted to the player-owned systems (no armada).
#[allow(clippy::too_many_arguments)]
fn player_tick(
    dir: i32,
    want_fire: bool,
    player: &mut Player,
    bullets: &mut BulletsManager,
    bombs: &mut BulletsManager,
    bonuses: &mut BonusManager,
    explosions: &mut ExplosionsManager,
    sink: &mut EventSink,
) {
    player.do_bombs_collisions(bombs, explosions, sink);
    player.do_bonus_collisions(bonuses, explosions, sink, None, None);
    bonuses.advance();
    player.move_player(dir);
    bullets.advance(Coord::new(-1, -1));
    bombs.advance(player.position());
    if want_fire {
        player.fire(bullets, sink);
    }
    explosions.advance();
}

#[allow(clippy::too_many_arguments)]
fn record_player_frame(
    sec: &mut Rows,
    f: i32,
    phase: i32,
    dir: i32,
    want_fire: bool,
    player: &Player,
    bullets: &BulletsManager,
    bombs: &BulletsManager,
    bonuses: &BonusManager,
    explosions: &ExplosionsManager,
    sink: &mut EventSink,
) {
    let row = sec.next("ent.pl.frame");
    assert_eq!(i(&row, 1), f);
    assert_eq!(i(&row, 2), phase);
    assert_eq!(i(&row, 3), dir);
    assert_eq!(i(&row, 4), i32::from(want_fire));
    assert_eq!(i(&row, 5), player.position().x, "f{f} x");
    assert_eq!(i(&row, 6), player.position().y, "f{f} y");
    assert_eq!(i(&row, 7), player.shield(), "f{f} shield");
    assert_eq!(i(&row, 8), player.fire_level(), "f{f} fire_lvl");
    assert_eq!(i(&row, 9), player.multi_fire(), "f{f} multi");
    assert_eq!(i(&row, 10), player.speed_level(), "f{f} speed_lvl");
    assert_eq!(i(&row, 11), bullets.nb(), "f{f} bullets");
    assert_eq!(i(&row, 12), bombs.nb(), "f{f} bombs");
    assert_eq!(i(&row, 13), bonuses.nb(), "f{f} bonuses");
    assert_eq!(sz(&row, 14), explosions.active_count(), "f{f} explosions");

    for a in 0..bullets.nb() as usize {
        let row = sec.next("ent.pl.bullet");
        let b = bullets.get_active(a);
        assert_eq!(i(&row, 1), f);
        assert_eq!(i(&row, 2), a as i32);
        assert_eq!(i(&row, 3), b.pos.x, "f{f} bullet{a} x");
        assert_eq!(i(&row, 4), b.pos.y, "f{f} bullet{a} y");
        assert_eq!(i(&row, 5), b.speed.x, "f{f} bullet{a} vx");
        assert_eq!(i(&row, 6), b.speed.y, "f{f} bullet{a} vy");
    }
    for a in 0..bombs.nb() as usize {
        let row = sec.next("ent.pl.bomb");
        let b = bombs.get_active(a);
        assert_eq!(i(&row, 1), f);
        assert_eq!(i(&row, 2), a as i32);
        assert_eq!(i(&row, 3), b.pos.x, "f{f} bomb{a} x");
        assert_eq!(i(&row, 4), b.pos.y, "f{f} bomb{a} y");
        assert_eq!(i(&row, 5), b.wave_phase, "f{f} bomb{a} wave_phase");
    }
    for a in 0..bonuses.nb() as usize {
        let row = sec.next("ent.pl.bonus");
        let s = bonuses.get_active(a);
        assert_eq!(i(&row, 1), f);
        assert_eq!(i(&row, 2), a as i32);
        assert_eq!(i(&row, 3), s.bonus_type as i32, "f{f} bonus{a} type");
        assert_eq!(i(&row, 4), s.pos.x, "f{f} bonus{a} x");
        assert_eq!(i(&row, 5), s.pos.y, "f{f} bonus{a} y");
    }
    drain_sink(sink, sec, "ent.pl.sink", f);
}

#[test]
fn player_parity() {
    let rows = load_rows();
    let mut sec = section(&rows, "ent.pl.");
    let grid = formation::recompute(1.0);

    let mut bullets = BulletsManager::new(0xD1CE71);
    let mut bombs = BulletsManager::new(0xD1CE72);
    let mut bonuses = BonusManager::new();
    let mut explosions = ExplosionsManager::new(2, grid.width);
    explosions.seed_random(0xD1CE73);
    bullets.set_metrics(METRICS);
    bombs.set_metrics(METRICS);
    bonuses.set_metrics(METRICS);
    explosions.set_metrics(1.0);
    let mut sink = EventSink::default();
    let mut player = Player::new(METRICS);

    let row = sec.next("ent.pl.setup");
    assert_eq!(i(&row, 1), METRICS.win_w);
    assert_eq!(i(&row, 2), METRICS.win_h);
    assert_eq!(i(&row, 3), player.shield(), "initial shield");

    let mut f = 0i32;

    // Phase A -- scripted lateral movement + continuous fire (the base
    // 40-frame interval gates the real cadence).
    let mut state = 0x7777_7777u32;
    while f < 120 {
        state = state.wrapping_mul(1664525).wrapping_add(1013904223);
        let dir = ((state >> 24) % 3) as i32 - 1;
        player_tick(
            dir, true, &mut player, &mut bullets, &mut bombs, &mut bonuses,
            &mut explosions, &mut sink,
        );
        record_player_frame(
            &mut sec, f, 0, dir, true, &player, &bullets, &bombs, &bonuses,
            &explosions, &mut sink,
        );
        f += 1;
    }

    // Phase B -- full bonus economy: all five supply pods queued above the
    // frozen ship, collected in falling order.
    let drop_x = player.position().x;
    let drops = [
        BonusType::ExtraSpeed,
        BonusType::ExtraFire,
        BonusType::ExtraMulti,
        BonusType::ExtraShield,
        BonusType::ExtraNuke,
    ];
    for (k, dt) in drops.iter().enumerate() {
        let drop_y = 560 - k as i32 * 24;
        bonuses.add(Coord::new(drop_x, drop_y), *dt);
        let row = sec.next("ent.pl.drop");
        assert_eq!(i(&row, 1), f);
        assert_eq!(sz(&row, 2), k);
        assert_eq!(i(&row, 3), *dt as i32, "drop {k} type");
        assert_eq!(i(&row, 4), drop_y, "drop {k} y");
    }
    while f < 400 && bonuses.nb() > 0 {
        player_tick(
            0, true, &mut player, &mut bullets, &mut bombs, &mut bonuses,
            &mut explosions, &mut sink,
        );
        record_player_frame(
            &mut sec, f, 1, 0, true, &player, &bullets, &bombs, &bonuses,
            &explosions, &mut sink,
        );
        f += 1;
    }

    // Phase C -- bomb volley: shield economy plus the damage events. Three
    // bombs against the 4-point shield (3 initial + the phase-B restore)
    // keep shield >= 0: the death cinematic is shell territory.
    for k in 0..3i32 {
        bombs.add(
            TextureId::Bomb,
            Coord::new(drop_x, -40 - k * 120),
            Coord::new(0, 6),
            false,
            false,
            0,
            0.0,
            false,
            SEEKER_ENGINE_COLOR,
        );
    }
    let phase_c_end = f + 240;
    while f < phase_c_end {
        player_tick(
            0, false, &mut player, &mut bullets, &mut bombs, &mut bonuses,
            &mut explosions, &mut sink,
        );
        record_player_frame(
            &mut sec, f, 2, 0, false, &player, &bullets, &bombs, &bonuses,
            &explosions, &mut sink,
        );
        f += 1;
    }
    sec.done("ent.pl");
}

// ---- ent.fleet: full armada replay in production tick order ------------------

/// Twin of the dumper's InputScript (numeric-recipes LCG).
struct InputScript {
    state: u32,
}

impl InputScript {
    fn step(&mut self) -> i32 {
        self.state = self.state.wrapping_mul(1664525).wrapping_add(1013904223);
        ((self.state >> 24) % 3) as i32 - 1
    }
}

/// Scenario-coverage flags (the seed search only accepts a seed that fired
/// every special-entity mechanic at least once).
#[derive(Default)]
struct FleetOutcome {
    warp_spawned: bool,
    warp_used: bool,
    decay_spawned: bool,
    bonus_spawned: bool,
    frames: i32,
    finished: bool,
    kills: i32,
}

impl FleetOutcome {
    fn coverage(&self) -> bool {
        self.warp_spawned && self.warp_used && self.decay_spawned && self.bonus_spawned
    }
}

/// The synthetic convoy table (DumpEntityFleet): Alien14 first so warp-capable
/// ships exist, two Alien4 convoys so the electrosphere roll almost surely
/// fires, split/mirror variety on the spline arrivals, short waits.
fn fleet_convoys() -> Vec<ConvoySpec> {
    vec![
        ConvoySpec {
            wait: 0,
            texture_id: TextureId::Alien14,
            convoy_size_pc: 25,
            x_mirror: false,
            split: false,
            simultaneous: false,
            dock_align: 0,
            path_id: 0,
        },
        ConvoySpec {
            wait: 10,
            texture_id: TextureId::Alien4,
            convoy_size_pc: 30,
            x_mirror: true,
            split: true,
            simultaneous: false,
            dock_align: 0,
            path_id: 1,
        },
        ConvoySpec {
            wait: 10,
            texture_id: TextureId::Alien1,
            convoy_size_pc: 20,
            x_mirror: false,
            split: false,
            simultaneous: false,
            dock_align: 0,
            path_id: 2,
        },
        ConvoySpec {
            wait: 15,
            texture_id: TextureId::Alien4,
            convoy_size_pc: 25,
            x_mirror: true,
            split: false,
            simultaneous: false,
            dock_align: 0,
            path_id: 0,
        },
    ]
}

/// The three arrival splines (loop / arc / spire), constructed exactly like
/// the dumper's EntityArrival* helpers.
fn fleet_paths() -> Vec<Vec<Vec2f>> {
    vec![
        path::flight_path_loop(120.0, -60.0, 2),
        path::generate_smooth_spline(&[
            Vec2f::new(100.0, -50.0),
            Vec2f::new(260.0, 120.0),
            Vec2f::new(410.0, 90.0),
            Vec2f::new(620.0, 300.0),
            Vec2f::new(700.0, 900.0),
        ]),
        path::generate_smooth_spline(&[
            Vec2f::new(512.0, -40.0),
            Vec2f::new(540.0, 300.0),
            Vec2f::new(512.0, 640.0),
            Vec2f::new(512.0, 780.0),
        ]),
    ]
}

const FLEET_LEVEL: i32 = 3;
const FLEET_MAX_LEVEL: i32 = 15; // GameRules::Progression::kWavesPerStage
const FLEET_REPLAY_FRAMES: i32 = 900;

/// One seeded fleet replay driven by the kernel `EntitySession` (RUST.md
/// Fatia 5): the 900-frame corpus IS the golden contract for the production
/// tick order, so this harness only builds the managers as scenario inputs,
/// feeds the deterministic input script, and records/asserts rows. With
/// `rec = Some` it asserts every corpus row of the recorded replay.
fn fleet_replay(seed: u32, mut rec: Option<&mut Rows>, mut fs: Option<&mut Rows>) -> FleetOutcome {
    let convoys = fleet_convoys();
    let paths = fleet_paths();
    let grid = formation::recompute(METRICS.scale);

    let mut bullets = BulletsManager::new(0x8EED11);
    let mut bombs = BulletsManager::new(0x8EED22);
    let mut bonuses = BonusManager::new();
    let mut explosions = ExplosionsManager::new(2, grid.width);
    explosions.seed_random(0xE1E2E3);
    bullets.set_metrics(METRICS);
    bombs.set_metrics(METRICS);
    bonuses.set_metrics(METRICS);
    explosions.set_metrics(METRICS.scale);
    let sink = EventSink::default();

    let fleet = Fleet::new(
        FLEET_LEVEL,
        convoys.clone(),
        paths.clone(),
        METRICS,
        FLEET_MAX_LEVEL,
        seed,
    );
    let player = Player::new(METRICS);

    // Kamikaze blast radius: the fleet grid width times the SSOT multiplier.
    let lethal = grid.width as f32 * 1.4;

    // The kernel session owns the managers and the canonical tick order;
    // the harness keeps only scenario inputs and row assertions.
    // Fatia 10 inputs, inert for this scenario: the armada never finishes
    // inside the 900-frame budget (3 ships survive at ent.fleet.done), so the
    // transition trigger cannot fire and the committed rows stay untouched.
    let mut session = EntitySession::new(
        fleet, player, bullets, bombs, bonuses, explosions, sink, lethal, METRICS, 0, [6.0; 15],
    );

    if let Some(sec) = rec.as_deref_mut() {
        let row = sec.next("ent.fleet.setup");
        assert_eq!(i(&row, 1), FLEET_LEVEL, "setup level");
        assert_eq!(u(&row, 2) as u32, seed, "setup seed");
        assert_eq!(i(&row, 3), METRICS.win_w, "setup win_w");
        assert_eq!(i(&row, 4), METRICS.win_h, "setup win_h");
        assert_eq!(hex32(&row[5]), METRICS.scale.to_bits(), "setup scale");
        assert_eq!(i(&row, 6), FLEET_REPLAY_FRAMES, "setup cap");
        assert_eq!(hex32(&row[7]), lethal.to_bits(), "setup lethal radius");

        for (c, cv) in convoys.iter().enumerate() {
            let row = sec.next("ent.fleet.convoy");
            assert_eq!(sz(&row, 1), c, "convoy idx");
            assert_eq!(i(&row, 2), cv.wait, "convoy {c} wait");
            assert_eq!(i(&row, 3), cv.texture_id as i32, "convoy {c} texture");
            assert_eq!(i(&row, 4), cv.convoy_size_pc, "convoy {c} size_pc");
            assert_eq!(i(&row, 5), i32::from(cv.x_mirror), "convoy {c} mirror");
            assert_eq!(i(&row, 6), i32::from(cv.split), "convoy {c} split");
            assert_eq!(i(&row, 7), i32::from(cv.simultaneous), "convoy {c} simult");
            assert_eq!(i(&row, 8), cv.dock_align, "convoy {c} dock_align");
            assert_eq!(sz(&row, 9), cv.path_id, "convoy {c} path_id");
        }
        for (p, pts) in paths.iter().enumerate() {
            let row = sec.next("ent.fleet.path_count");
            assert_eq!(sz(&row, 1), p, "path id");
            assert_eq!(sz(&row, 2), pts.len(), "path {p} point count");
            for (j, pt) in pts.iter().enumerate() {
                let row = sec.next("ent.fleet.path");
                assert_eq!(sz(&row, 1), p, "path id");
                assert_eq!(sz(&row, 2), j, "path point idx");
                assert_eq!(hex32(&row[3]), pt.x.to_bits(), "path {p} pt {j} x");
                assert_eq!(hex32(&row[4]), pt.y.to_bits(), "path {p} pt {j} y");
            }
        }
    }

    // Fatia 8 companion section: the session-level score context. The setup
    // row is consumed here, the per-credited-frame rows ride inside the tick
    // loop and the award epilogue follows ent.fleet.done.
    if let Some(fsec) = fs.as_deref_mut() {
        let row = fsec.next("ent.fs.setup");
        assert_eq!(u(&row, 1) as u32, seed, "fs setup seed");
        assert_eq!(
            i(&row, 2),
            session.player().shield(),
            "fs initial shield"
        );
        assert_eq!(u(&row, 3), score::EXTRA_SHIELD_STEP, "fs shield step");
        assert_eq!(i(&row, 4), score::MAX_SHIELD, "fs max shield");
    }

    let mut out = FleetOutcome::default();
    let mut script = InputScript { state: 0x1234_ABCD };
    // Mirrors of the dumper's score-context gates: milestones drained during
    // the replay (must stay zero) and credited frames (earned > 0).
    let mut replay_extra = 0u64;
    let mut credit_frames = 0i32;

    let mut f = 0i32;
    while f < FLEET_REPLAY_FRAMES {
        let dir = script.step();

        // One frame of the canonical production order, kernel-side:
        // EntitySession::tick == the TickFixedPhysics entity subset.
        let report = session.tick(dir, true);
        replay_extra += report.extra_shields;
        if session.bonuses().nb() > 0
            || session.player().speed_level()
                + session.player().fire_level()
                + session.player().multi_fire()
                > 1
        {
            out.bonus_spawned = true;
        }
        for a in 0..session.fleet().alien_count() {
            let alien = session.fleet().get_alien(a);
            if alien.texture_id() == TextureId::Alien14 {
                if alien.warp_evasions_remaining() > 0 {
                    out.warp_spawned = true;
                }
                if alien.warp_evasions_remaining() < 2 {
                    out.warp_used = true;
                }
            } else if alien.texture_id() == TextureId::Alien4
                && (alien.can_trigger_alpha_decay() || alien.scale() < 1.0)
            {
                out.decay_spawned = true;
            }
        }

        if let Some(sec) = rec.as_deref_mut() {
            let row = sec.next("ent.fleet.frame");
            assert_eq!(i(&row, 1), f, "frame idx");
            assert_eq!(i(&row, 2), dir, "f{f} dir");
            assert_eq!(i(&row, 3), 1, "f{f} fire flag");
            assert_eq!(sz(&row, 4), session.fleet().alien_count(), "f{f} aliens");
            assert_eq!(i(&row, 5), session.bombs().nb(), "f{f} bombs");
            assert_eq!(i(&row, 6), session.bullets().nb(), "f{f} bullets");
            assert_eq!(i(&row, 7), session.bonuses().nb(), "f{f} bonuses");
            assert_eq!(
                sz(&row, 8),
                session.explosions().active_count(),
                "f{f} explosions"
            );
            assert_eq!(i(&row, 9), session.player().position().x, "f{f} pl_x");
            assert_eq!(i(&row, 10), session.player().position().y, "f{f} pl_y");
            assert_eq!(i(&row, 11), session.player().shield(), "f{f} shield");
            assert_eq!(i(&row, 12), session.player().fire_level(), "f{f} fire_lvl");
            assert_eq!(i(&row, 13), session.player().multi_fire(), "f{f} multi");
            assert_eq!(i(&row, 14), session.player().speed_level(), "f{f} speed_lvl");
            assert_eq!(i(&row, 15), report.collisions.total_hits, "f{f} hits");
            assert_eq!(i(&row, 16), report.collisions.regular_hits, "f{f} regular");
            assert_eq!(
                i(&row, 17),
                report.collisions.kamikaze_hits,
                "f{f} kamikaze"
            );
            assert_eq!(u(&row, 18), session.score().value(), "f{f} score");
            assert_eq!(
                i(&row, 19),
                i32::from(report.kamikaze_proximity),
                "f{f} proximity"
            );

            if report.collisions.score_earned > 0 {
                credit_frames += 1;
                if let Some(fsec) = fs.as_deref_mut() {
                    let row = fsec.next("ent.fs.frame");
                    assert_eq!(i(&row, 1), f, "fs frame idx");
                    assert_eq!(u(&row, 2), session.score().value(), "f{f} fs total");
                    assert_eq!(
                        hex64(&row[3]),
                        session.score().shield_progress().to_bits(),
                        "f{f} fs progress"
                    );
                    assert_eq!(i(&row, 4), session.player().shield(), "f{f} fs shield");
                    assert_eq!(u(&row, 5), report.extra_shields, "f{f} fs extra");
                }
            }

            for a in 0..session.fleet().alien_count() {
                let alien = session.fleet().get_alien(a);
                let row = sec.next("ent.fleet.alien");
                assert_eq!(i(&row, 1), f, "alien frame");
                assert_eq!(sz(&row, 2), a, "alien idx");
                assert_eq!(i(&row, 3), alien.texture_id() as i32, "f{f} alien{a} tex");
                assert_eq!(i(&row, 4), alien.position().x, "f{f} alien{a} x");
                assert_eq!(i(&row, 5), alien.position().y, "f{f} alien{a} y");
                assert_eq!(i(&row, 6), alien.width(), "f{f} alien{a} w");
                assert_eq!(i(&row, 7), alien.height(), "f{f} alien{a} h");
                assert_eq!(i(&row, 8), alien.stage() as i32, "f{f} alien{a} stage");
                assert_eq!(i(&row, 9), i32::from(alien.is_kamikaze()), "f{f} alien{a} kam");
                assert_eq!(i(&row, 10), i32::from(alien.is_warping()), "f{f} alien{a} warp");
                assert_eq!(i(&row, 11), alien.warp_evasions_remaining(), "f{f} alien{a} ev");
                assert_eq!(
                    i(&row, 12),
                    i32::from(alien.can_trigger_alpha_decay()),
                    "f{f} alien{a} decay"
                );
                assert_angle_bits(&row[13], alien.angle().to_bits(), &format!("f{f} alien{a} angle"));
                assert_eq!(hex32(&row[14]), alien.scale().to_bits(), "f{f} alien{a} scale");
            }
            for a in 0..session.bombs().nb() as usize {
                let b = session.bombs().get_active(a);
                let row = sec.next("ent.fleet.bomb");
                assert_eq!(i(&row, 1), f, "bomb frame");
                assert_eq!(i(&row, 2), a as i32, "bomb idx");
                assert_eq!(i(&row, 3), b.pos.x, "f{f} bomb{a} x");
                assert_eq!(i(&row, 4), b.pos.y, "f{f} bomb{a} y");
                assert_eq!(i(&row, 5), i32::from(b.relativistic), "f{f} bomb{a} rel");
                assert_eq!(i(&row, 6), i32::from(b.radioactive), "f{f} bomb{a} radio");
                assert_eq!(i(&row, 7), i32::from(b.is_turning_bomb), "f{f} bomb{a} turn");
                assert_eq!(i(&row, 8), b.turn_timer, "f{f} bomb{a} timer");
                assert_eq!(i(&row, 9), b.wave_phase, "f{f} bomb{a} phase");
            }
            for a in 0..session.bullets().nb() as usize {
                let b = session.bullets().get_active(a);
                let row = sec.next("ent.fleet.bullet");
                assert_eq!(i(&row, 1), f, "bullet frame");
                assert_eq!(i(&row, 2), a as i32, "bullet idx");
                assert_eq!(i(&row, 3), b.pos.x, "f{f} bullet{a} x");
                assert_eq!(i(&row, 4), b.pos.y, "f{f} bullet{a} y");
                assert_eq!(i(&row, 5), b.speed.x, "f{f} bullet{a} vx");
                assert_eq!(i(&row, 6), b.speed.y, "f{f} bullet{a} vy");
            }
            for a in 0..session.bonuses().nb() as usize {
                let s = session.bonuses().get_active(a);
                let row = sec.next("ent.fleet.bonus");
                assert_eq!(i(&row, 1), f, "bonus frame");
                assert_eq!(i(&row, 2), a as i32, "bonus idx");
                assert_eq!(i(&row, 3), s.bonus_type as i32, "f{f} bonus{a} type");
                assert_eq!(i(&row, 4), s.pos.x, "f{f} bonus{a} x");
                assert_eq!(i(&row, 5), s.pos.y, "f{f} bonus{a} y");
            }
            drain_sink(session.sink_mut(), sec, "ent.fleet.sink", f);
        } else {
            session.sink_mut().drain();
        }

        // Early exit: armada finished and every pool drained (only after the
        // search had a fair combat window for the coverage contract).
        if f >= 500
            && session.fleet().finished()
            && session.bombs().nb() == 0
            && session.bullets().nb() == 0
            && session.bonuses().nb() == 0
            && session.explosions().active_count() == 0
        {
            f += 1;
            break;
        }
        f += 1;
    }

    out.frames = f;
    out.finished = session.fleet().finished();
    out.kills = session.kills();
    if let Some(sec) = rec.as_deref_mut() {
        let row = sec.next("ent.fleet.done");
        assert_eq!(i(&row, 1), out.frames, "done frames");
        assert_eq!(i(&row, 2), i32::from(out.finished), "done finished");
        assert_eq!(i(&row, 3), out.kills, "done kills");
        sec.done("ent.fleet");
    }
    if let Some(fsec) = fs.as_deref_mut() {
        // The replay window must stay milestone-free: that is the proof the
        // committed ent.fleet.* rows are untouched by the Score wiring.
        assert_eq!(replay_extra, 0, "replay drained milestones");
        let mut awarded = 0u64;
        while fsec.peek_is("ent.fs.award") {
            let row = fsec.next("ent.fs.award");
            let idx = i(&row, 1);
            let ms = session.award_points(u(&row, 2));
            assert_eq!(u(&row, 3), ms, "award {idx} milestones");
            assert_eq!(
                i(&row, 4),
                session.player().shield(),
                "award {idx} shield"
            );
            assert_eq!(
                hex64(&row[5]),
                session.score().shield_progress().to_bits(),
                "award {idx} progress"
            );
            assert_eq!(u(&row, 6), session.score().value(), "award {idx} total");
            awarded += ms;
        }
        let row = fsec.next("ent.fs.done");
        assert_eq!(i(&row, 1), out.frames, "fs done frames");
        assert_eq!(i(&row, 2), credit_frames, "fs credited frames");
        assert_eq!(u(&row, 3), session.score().value(), "fs final total");
        assert_eq!(i(&row, 4), session.player().shield(), "fs final shield");
        assert_eq!(u(&row, 5), awarded, "fs awarded milestones");
        fsec.done("ent.fs");
    }
    out
}

#[test]
fn fleet_parity() {
    let rows = load_rows();

    // Deterministic seed search: the first seed that exercises warp evasion
    // (born AND consumed), alpha decay and the bonus economy inside the
    // replay budget -- the exact loop of DumpEntityFleet.
    let mut chosen = 0u32;
    for s in 1u32..=2048 {
        let probe = fleet_replay(s, None, None);
        if probe.coverage() {
            chosen = s;
            break;
        }
    }
    assert_ne!(chosen, 0, "no fleet seed satisfied the coverage contract");

    let mut sec = section(&rows, "ent.fleet.");
    let mut fs_sec = section(&rows, "ent.fs.");
    let recorded = fleet_replay(chosen, Some(&mut sec), Some(&mut fs_sec));
    assert!(recorded.coverage(), "recorded replay lost coverage");
}

// ---- Phase transition replay (Fatia 10) --------------------------------------

const PT_LEVEL: i32 = 1;
const PT_MAX_LEVEL: i32 = 15;
const PT_RNG_SEED: u32 = 0x9EED99;
const PT_BULLETS_SEED: u32 = 0x97EED11;
const PT_BOMBS_SEED: u32 = 0x97EED22;
const PT_EXPLOSIONS_SEED: u32 = 0xE2E3E4;
const PT_CAP: i32 = 1500;
const PT_TAIL: i32 = 10;
/// The scripted bake-edge duration (6.0 s + one 44.1 kHz sample) that pushes
/// the wave-1 hold to 361 frames -- the dumper computes the same f32 sum.
const PT_FANFARE_EDGE: f32 = 6.0 + 1.0 / 44100.0;

/// Five single-alien convoys honoring the catalog invariant (table length ==
/// level-1 profile convoy_count == 5), pc=8 -> one ship per convoy at 1280 px.
fn pt_convoys() -> Vec<ConvoySpec> {
    (0..5)
        .map(|c| ConvoySpec {
            wait: c * 20,
            texture_id: TextureId::Alien1,
            convoy_size_pc: 8,
            x_mirror: false,
            split: false,
            simultaneous: false,
            dock_align: 0,
            path_id: 0,
        })
        .collect()
}

/// The dumper's EntityArrivalSpire, resampled through the same golden-pinned
/// spline (its controls are spline.* case 2).
fn pt_paths() -> Vec<Vec<Vec2f>> {
    let ctrls: Vec<Vec2f> = [[512.0f32, -40.0], [540.0, 300.0], [512.0, 640.0], [512.0, 780.0]]
        .iter()
        .map(|p| Vec2f::new(p[0], p[1]))
        .collect();
    vec![path::generate_smooth_spline(&ctrls)]
}

/// Scenario-coverage flags; the seed search accepts a seed only when the full
/// arc fired -- the dumper's RunPhaseTransitionReplay gate, mirrored.
#[derive(Default)]
struct PtOutcome {
    trigger_frame: i32,
    rebuild_frame: i32,
    kills: i32,
    destroyed_frames: i32,
    frames: i32,
    final_level: i32,
    final_score: u64,
}

impl PtOutcome {
    fn covered(&self) -> bool {
        self.trigger_frame >= 0
            && self.rebuild_frame == self.trigger_frame + 360
            && self.kills == 5
            && self.final_level == 2
            && self.frames < PT_CAP
    }
}

fn pt_replay(seed: u32, mut rec: Option<&mut Rows>) -> PtOutcome {
    let convoys = pt_convoys();
    let paths = pt_paths();
    let grid = formation::recompute(METRICS.scale);

    let mut bullets = BulletsManager::new(PT_BULLETS_SEED);
    let mut bombs = BulletsManager::new(PT_BOMBS_SEED);
    let mut bonuses = BonusManager::new();
    let mut explosions = ExplosionsManager::new(2, grid.width);
    explosions.seed_random(PT_EXPLOSIONS_SEED);
    bullets.set_metrics(METRICS);
    bombs.set_metrics(METRICS);
    bonuses.set_metrics(METRICS);
    explosions.set_metrics(METRICS.scale);
    let sink = EventSink::default();

    let fleet = Fleet::new(PT_LEVEL, convoys.clone(), paths.clone(), METRICS, PT_MAX_LEVEL, seed);
    let player = Player::new(METRICS);
    let lethal = grid.width as f32 * 1.4;

    // Fanfare durations: 6.0 s everywhere except wave 1, which carries the
    // 44.1 kHz bake edge. The recording run re-asserts every slot against the
    // ent.pt.fanfare rows.
    let mut durations = [stage_catalog::K_STAGE_FANFARE_DURATION_SECONDS; 15];
    durations[0] = PT_FANFARE_EDGE;

    let mut session = EntitySession::new(
        fleet, player, bullets, bombs, bonuses, explosions, sink, lethal, METRICS, PT_RNG_SEED,
        durations,
    );

    if let Some(sec) = rec.as_deref_mut() {
        let row = sec.next("ent.pt.setup");
        assert_eq!(u(&row, 1) as u32, seed, "setup fleet seed");
        assert_eq!(u(&row, 2) as u32, PT_RNG_SEED, "setup rng seed");
        assert_eq!(u(&row, 3) as u32, PT_BULLETS_SEED, "setup bullets seed");
        assert_eq!(u(&row, 4) as u32, PT_BOMBS_SEED, "setup bombs seed");
        assert_eq!(u(&row, 5) as u32, PT_EXPLOSIONS_SEED, "setup explosions seed");
        assert_eq!(i(&row, 6), METRICS.win_w, "setup win_w");
        assert_eq!(i(&row, 7), METRICS.win_h, "setup win_h");
        assert_eq!(hex32(&row[8]), METRICS.scale.to_bits(), "setup scale");
        assert_eq!(hex32(&row[9]), lethal.to_bits(), "setup lethal radius");
        assert_eq!(i(&row, 10), session.player().shield(), "setup initial shield");
        assert_eq!(i(&row, 11), PT_LEVEL, "setup level");
        assert_eq!(i(&row, 12), PT_CAP, "setup cap");
        assert_eq!(i(&row, 13), PT_TAIL, "setup tail");
        for (k, d) in durations.iter().enumerate() {
            let row = sec.next("ent.pt.fanfare");
            assert_eq!(sz(&row, 1), k, "fanfare idx");
            assert_eq!(hex32(&row[2]), d.to_bits(), "fanfare {k} duration bits");
        }
        for (c, cv) in convoys.iter().enumerate() {
            let row = sec.next("ent.pt.convoy");
            assert_eq!(sz(&row, 1), c, "convoy idx");
            assert_eq!(i(&row, 2), cv.wait, "convoy {c} wait");
            assert_eq!(i(&row, 3), cv.texture_id as i32, "convoy {c} texture");
            assert_eq!(i(&row, 4), cv.convoy_size_pc, "convoy {c} size_pc");
            assert_eq!(i(&row, 5), i32::from(cv.x_mirror), "convoy {c} mirror");
            assert_eq!(i(&row, 6), i32::from(cv.split), "convoy {c} split");
            assert_eq!(i(&row, 7), i32::from(cv.simultaneous), "convoy {c} simult");
            assert_eq!(i(&row, 8), cv.dock_align, "convoy {c} dock_align");
            assert_eq!(sz(&row, 9), cv.path_id, "convoy {c} path_id");
        }
        for (p, pts) in paths.iter().enumerate() {
            let row = sec.next("ent.pt.path_count");
            assert_eq!(sz(&row, 1), p, "path id");
            assert_eq!(sz(&row, 2), pts.len(), "path {p} point count");
            for (j, pt) in pts.iter().enumerate() {
                let row = sec.next("ent.pt.path");
                assert_eq!(sz(&row, 1), p, "path id");
                assert_eq!(sz(&row, 2), j, "path point idx");
                assert_eq!(hex32(&row[3]), pt.x.to_bits(), "path {p} pt {j} x");
                assert_eq!(hex32(&row[4]), pt.y.to_bits(), "path {p} pt {j} y");
            }
        }
    }

    let mut out = PtOutcome { trigger_frame: -1, rebuild_frame: -1, ..PtOutcome::default() };
    let mut f = 0i32;
    while f < PT_CAP {
        let level_before = session.score().level();
        // The stationary player shares one lane with the bullet column: the
        // divers come to the shots, which keeps the finish fast and the
        // scenario deterministic without an input script.
        let report = session.tick(0, true);
        out.kills += report.collisions.total_hits;
        if report.player_destroyed {
            out.destroyed_frames += 1;
        }
        if report.transition_started {
            out.trigger_frame = f;
        }
        if report.rebuilt {
            out.rebuild_frame = f;
        }

        if let Some(sec) = rec.as_deref_mut() {
            if report.transition_started {
                let row = sec.next("ent.pt.trigger");
                assert_eq!(i(&row, 1), f, "trigger frame");
                assert_eq!(i(&row, 2), level_before, "completed level");
                let slot = stage_catalog::wave_in_stage(level_before).max(0) as usize;
                let dur = durations[slot];
                assert_eq!(hex32(&row[3]), dur.to_bits(), "f{f} fanfare duration bits");
                assert_eq!(
                    hex32(&row[4]),
                    stage_catalog::K_STAGE_FANFARE_DURATION_SECONDS.max(dur).to_bits(),
                    "f{f} hold bits"
                );
                assert_eq!(i(&row, 5), report.transition_frames, "f{f} hold frames");
                assert_eq!(i(&row, 6), session.score().level(), "f{f} new level");
                assert_eq!(i(&row, 7), WARP_DURATION_FRAMES, "f{f} warp frames");
            }
            if report.rebuilt {
                let row = sec.next("ent.pt.rebuild");
                assert_eq!(i(&row, 1), f, "rebuild frame");
                assert_eq!(i(&row, 2), report.rebuild_level, "f{f} rebuild level");
                assert_eq!(u(&row, 3) as u32, report.rebuild_seed, "f{f} rebuild seed");
                assert_eq!(
                    sz(&row, 4),
                    session.fleet().alien_count(),
                    "f{f} alive right after rebuild"
                );
                assert_eq!(
                    i(&row, 5),
                    stage_catalog::get_profile(report.rebuild_level, PT_MAX_LEVEL).convoy_count,
                    "f{f} rebuilt profile convoy_count"
                );
            }
            let row = sec.next("ent.pt.frame");
            assert_eq!(i(&row, 1), f, "frame idx");
            assert_eq!(sz(&row, 2), session.fleet().alien_count(), "f{f} aliens");
            assert_eq!(i(&row, 3), session.bombs().nb(), "f{f} bombs");
            assert_eq!(i(&row, 4), session.bullets().nb(), "f{f} bullets");
            assert_eq!(i(&row, 5), report.transition_remaining, "f{f} remaining");
            assert_eq!(i(&row, 6), session.score().level(), "f{f} level");
            assert_eq!(i(&row, 7), session.player().shield(), "f{f} shield");
            assert_eq!(u(&row, 8), session.score().value(), "f{f} score");
            assert_eq!(i(&row, 9), report.collisions.total_hits, "f{f} hits");
            assert_eq!(i(&row, 10), i32::from(report.kamikaze_proximity), "f{f} proximity");
            assert_eq!(i(&row, 11), i32::from(report.player_destroyed), "f{f} destroyed");
            assert_eq!(i(&row, 12), i32::from(report.transition_started), "f{f} triggered");
            assert_eq!(i(&row, 13), i32::from(report.rebuilt), "f{f} rebuilt");
            drain_sink(session.sink_mut(), sec, "ent.pt.sink", f);
        } else {
            session.sink_mut().drain();
        }

        if out.rebuild_frame >= 0 && f - out.rebuild_frame >= PT_TAIL {
            f += 1;
            break;
        }
        f += 1;
    }

    out.frames = f;
    out.final_level = session.score().level();
    out.final_score = session.score().value();
    if let Some(sec) = rec.as_deref_mut() {
        let row = sec.next("ent.pt.done");
        assert_eq!(i(&row, 1), out.frames, "done frames");
        assert_eq!(i(&row, 2), out.trigger_frame, "done trigger frame");
        assert_eq!(i(&row, 3), out.rebuild_frame, "done rebuild frame");
        assert_eq!(i(&row, 4), out.kills, "done kills");
        assert_eq!(i(&row, 5), out.final_level, "done final level");
        assert_eq!(u(&row, 6), out.final_score, "done final score");
        assert_eq!(i(&row, 7), out.destroyed_frames, "done destroyed frames");
        sec.done("ent.pt");
    }
    out
}

/// The phase-transition contract (RUST.md Fatia 10): the hold formula over
/// the bake-edge durations (pt.* rows, zero tolerance -- max/ceil on f32),
/// then the full live arc (ent.pt.*): a finished wave-1 armada triggers the
/// 361-frame hold, the countdown freezes the armada, the warp flag rides the
/// trigger frame, IncLevel lands before the countdown ends, and the rebuild
/// re-arms from the REAL level-2 catalog with one stream draw -- post-fanfare
/// delay, convoy schedule and all, for the tail frames.
#[test]
fn phase_transition_parity() {
    let rows = load_rows();

    let mut pt = section(&rows, "pt.");
    let mut cases = 0;
    while pt.peek_is("pt.hold") {
        let row = pt.next("pt.hold");
        let d = hb(&row[2]);
        let hold = stage_catalog::K_STAGE_FANFARE_DURATION_SECONDS.max(d);
        assert_eq!(hex32(&row[3]), hold.to_bits(), "case {cases} hold bits");
        assert_eq!(i(&row, 4), phase_transition_frames(d), "case {cases} frames");
        cases += 1;
    }
    let row = pt.next("pt.warp");
    assert_eq!(i(&row, 1), WARP_DURATION_FRAMES, "warp frames");
    let row = pt.next("pt.done");
    assert_eq!(i(&row, 1), cases, "formula case count");
    pt.done("pt");

    // Deterministic seed search: the first seed whose arc completes (all five
    // ships down, 361-frame hold, catalog rebuild) -- the exact loop of
    // DumpEntityPhaseTransition.
    let mut chosen = 0u32;
    for s in 1u32..=64 {
        let probe = pt_replay(s, None);
        if probe.covered() {
            chosen = s;
            break;
        }
    }
    assert_ne!(chosen, 0, "no phase-transition seed completed the arc");

    let mut sec = section(&rows, "ent.pt.");
    let recorded = pt_replay(chosen, Some(&mut sec));
    assert!(recorded.covered(), "recorded replay lost coverage");
}

// ---- Starfield replay (Fatia 6) ---------------------------------------------

/// FNV-1a (64) over the full dynamic state of every active star, in pool
/// order -- the test-side twin of the dumper's `StarPoolHash`. Float bits go
/// little-endian, byte by byte, so both sides fold identical bytes.
fn fnv_byte(h: &mut u64, b: u8) {
    // FNV-1a multiplies modulo 2^64 by design: C++ unsigned wraps, so the
    // twin must wrap explicitly (debug builds check overflow).
    *h = (*h ^ b as u64).wrapping_mul(1_099_511_628_211);
}

fn fnv_u32(h: &mut u64, v: u32) {
    for k in 0..4 {
        fnv_byte(h, ((v >> (8 * k)) & 0xFF) as u8);
    }
}

fn star_pool_hash(sf: &StarsFields) -> u64 {
    let mut h: u64 = 14_695_981_039_346_656_037;
    for i in 0..sf.active_stars() {
        let s = sf.sample_star(i);
        fnv_u32(&mut h, s.x.to_bits());
        fnv_u32(&mut h, s.y.to_bits());
        fnv_u32(&mut h, s.base_speed.to_bits());
        fnv_byte(&mut h, s.star_type);
        fnv_byte(&mut h, s.spectral);
        fnv_byte(&mut h, s.r);
        fnv_byte(&mut h, s.g);
        fnv_byte(&mut h, s.b);
        fnv_byte(&mut h, s.alpha);
        fnv_u32(&mut h, s.twinkle_phase as u32);
        fnv_u32(&mut h, s.twinkle_step as u32);
    }
    h
}

fn hex64(tok: &str) -> u64 {
    u64::from_str_radix(tok, 16).unwrap_or_else(|e| panic!("bad u64 hex {tok}: {e}"))
}

fn f64_of(row: &[String], k: usize) -> f64 {
    row[k].parse::<f64>().unwrap_or_else(|e| panic!("bad f64 {:?}: {e}", row[k]))
}

/// Asserts the 11 data columns shared by `ent.stars.star` and
/// `ent.stars.snapshot` rows against the twin's pool slot.
fn assert_star_row(row: &[String], sf: &StarsFields, idx: usize, f: i32, ctx: &str) {
    let s = sf.sample_star(idx);
    assert_eq!(i(row, 1), f, "{ctx} frame");
    assert_eq!(sz(row, 2), idx, "{ctx} idx");
    assert_eq!(hex32(&row[3]), s.x.to_bits(), "{ctx} x");
    assert_eq!(hex32(&row[4]), s.y.to_bits(), "{ctx} y");
    assert_eq!(hex32(&row[5]), s.base_speed.to_bits(), "{ctx} speed");
    assert_eq!(sz(row, 6), s.star_type as usize, "{ctx} type");
    assert_eq!(sz(row, 7), s.spectral as usize, "{ctx} spectral");
    assert_eq!(i(row, 8), s.r as i32, "{ctx} r");
    assert_eq!(i(row, 9), s.g as i32, "{ctx} g");
    assert_eq!(i(row, 10), s.b as i32, "{ctx} b");
    assert_eq!(i(row, 11), s.twinkle_phase as i32, "{ctx} phase");
    assert_eq!(i(row, 12), s.twinkle_step as i32, "{ctx} step");
    assert_eq!(i(row, 13), s.alpha as i32, "{ctx} alpha");
}

/// The starfield contract (RUST.md Fatia 6): frozen twinkle LUT + frozen
/// warp easing, then the scripted 640-frame replay -- bit-exact on EVERY
/// column (zero tolerance; the transcendentals live in the frozen tables).
#[test]
fn stars_parity() {
    let rows = load_rows();
    let mut sec = section(&rows, "ent.stars.");

    // ---- setup echoes: SSOT drift gate for the duplicated constants ----
    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "seed");
    assert_eq!(hex32(&row[2]), STARFIELD_SEED, "star rng seed");

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "win");
    assert_eq!(i(&row, 2), 1280, "win_w");
    assert_eq!(i(&row, 3), 720, "win_h");

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "frames");
    let frames = i(&row, 2);
    assert_eq!(frames, 640, "replay budget");

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "warp_frames");
    assert_eq!(i(&row, 2), WARP_DURATION_FRAMES, "production warp length");

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "warp_at");
    let warp_at = i(&row, 2);

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "resize_at");
    let resize_at = i(&row, 2);
    let new_w = i(&row, 3);
    let new_h = i(&row, 4);

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "density_at");
    let density_at = i(&row, 2);

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "warp_mult");
    assert_eq!(hex32(&row[2]), stars::K_WARP_MULT.to_bits(), "warp multiplier");

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "base_res");
    assert_eq!(f64_of(&row, 2), stars::K_BASE_RES_W, "base res w");
    assert_eq!(f64_of(&row, 3), stars::K_BASE_RES_H, "base res h");

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "base_count");
    assert_eq!(f64_of(&row, 2), stars::K_BASE_STAR_COUNT, "base star count");

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "min_max");
    assert_eq!(sz(&row, 2), stars::K_MIN_STARS, "min stars");
    assert_eq!(sz(&row, 3), stars::K_MAX_STARS, "max stars");

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "threshold");
    assert_eq!(
        hex32(&row[2]),
        stars::K_FOREGROUND_THRESHOLD.to_bits(),
        "foreground threshold"
    );

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "tau");
    assert_eq!(hex32(&row[2]), stars::K_TAU.to_bits(), "tau");

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "twinkle_floor_ceil");
    assert_eq!(hex32(&row[2]), stars::K_TWINKLE_FLOOR.to_bits(), "floor");
    assert_eq!(hex32(&row[3]), stars::K_TWINKLE_CEIL.to_bits(), "ceil");

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "harmonics");
    assert_eq!(hex32(&row[2]), stars::K_TWINKLE_HARMONIC2.to_bits(), "h2");
    assert_eq!(hex32(&row[3]), stars::K_TWINKLE_HARMONIC3.to_bits(), "h3");

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "mid_speed");
    assert_eq!(hex32(&row[2]), stars::K_MID_SPEED_MIN.to_bits(), "mid min");
    assert_eq!(hex32(&row[3]), stars::K_MID_SPEED_MAX.to_bits(), "mid max");

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "fg_speed");
    assert_eq!(hex32(&row[2]), stars::K_FG_SPEED_MIN.to_bits(), "fg min");
    assert_eq!(hex32(&row[3]), stars::K_FG_SPEED_MAX.to_bits(), "fg max");

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "twinkle_speed");
    assert_eq!(hex32(&row[2]), stars::K_TWINKLE_SPEED_MIN.to_bits(), "tw min");
    assert_eq!(hex32(&row[3]), stars::K_TWINKLE_SPEED_MAX.to_bits(), "tw max");

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "phase_scale");
    assert_eq!(
        i(&row, 2),
        stars::K_TWINKLE_PHASE_SCALE as i32,
        "phase scale"
    );

    let row = sec.next("ent.stars.setup");
    assert_eq!(row[1], "density_factors");
    for lvl in 0..5usize {
        assert_eq!(
            hex64(&row[2 + lvl]),
            stars::K_DENSITY_FACTORS[lvl].to_bits(),
            "density factor level {lvl}"
        );
    }

    // ---- frozen twinkle LUT (recorded transcendentals, bit-exact) ----
    for k in 0..256 {
        let row = sec.next("ent.stars.alpha");
        assert_eq!(i(&row, 1), k, "alpha idx");
        assert_eq!(
            i(&row, 2),
            StarsFields::twinkle_alpha(k as u8) as i32,
            "alpha[{k}]"
        );
    }
    for k in 0..256 {
        let row = sec.next("ent.stars.pulse");
        assert_eq!(i(&row, 1), k, "pulse idx");
        assert_eq!(
            hex32(&row[2]),
            StarsFields::twinkle_pulse(k as u8).to_bits(),
            "pulse[{k}]"
        );
    }

    // ---- frozen warp easing via a throwaway probe (rng-independent) ----
    let mut probe = StarsFields::new(1280, 720);
    probe.set_config_details(2);
    probe.trigger_hyperspace_warp();
    for _ in 0..WARP_DURATION_FRAMES {
        probe.scroll();
        let row = sec.next("ent.stars.warp");
        assert_eq!(i(&row, 1), probe.warp_timer(), "warp timer");
        assert_eq!(
            hex32(&row[2]),
            probe.warp_factor().to_bits(),
            "warp factor @t={}",
            probe.warp_timer()
        );
    }

    // ---- scripted replay: the dumper's scenario, frame by frame ----
    let mut sf = StarsFields::new(1280, 720);
    sf.set_config_details(2);
    let mut details = 2;
    let mut respawns_total = 0i64;
    let mut warp_seen = false;
    let mut expanded = false;
    let mut shrunk = false;
    for f in 0..frames {
        if f == warp_at {
            sf.trigger_hyperspace_warp();
        }
        if f == resize_at {
            sf.set_viewport(new_w, new_h);
        }
        if f == density_at {
            details = 1;
            sf.set_details_level(details);
        }
        sf.scroll();
        if sf.warp_timer() > 0 || sf.warp_factor() > 1.0 {
            warp_seen = true;
        }
        // Respawn observable: the headless fall respawn parks y at exactly -5.
        let mut respawned = 0i32;
        for k in 0..sf.active_stars() {
            if sf.sample_star(k).y == -5.0 {
                respawned += 1;
            }
        }
        respawns_total += i64::from(respawned);
        if sf.active_stars() == 225 {
            expanded = true;
        }
        if f >= density_at && sf.active_stars() == 90 {
            shrunk = true;
        }

        let row = sec.next("ent.stars.frame");
        assert_eq!(i(&row, 1), f, "frame idx");
        assert_eq!(i(&row, 2), sf.warp_timer(), "f{f} warp_timer");
        assert_eq!(hex32(&row[3]), sf.warp_factor().to_bits(), "f{f} warp_factor");
        assert_eq!(sz(&row, 4), sf.active_stars(), "f{f} active");
        assert_eq!(i(&row, 5), sf.viewport_width(), "f{f} win_w");
        assert_eq!(i(&row, 6), sf.viewport_height(), "f{f} win_h");
        assert_eq!(i(&row, 7), details, "f{f} details");
        assert_eq!(i(&row, 8), respawned, "f{f} respawned");
        assert_eq!(hex64(&row[9]), star_pool_hash(&sf), "f{f} pool hash");

        let snapshot = f == 0 || f == 420 || f == resize_at || f == density_at || f == frames - 1;
        let count = if snapshot { sf.active_stars() } else { 8 };
        for k in 0..count {
            let tag = if snapshot { "ent.stars.snapshot" } else { "ent.stars.star" };
            let row = sec.next(tag);
            assert_star_row(&row, &sf, k, f, tag);
        }
    }

    let row = sec.next("ent.stars.done");
    assert_eq!(i(&row, 1), frames, "done frames");
    assert_eq!(row[2].parse::<i64>().unwrap(), respawns_total, "done respawns");
    assert_eq!(sz(&row, 3), sf.active_stars(), "done active");
    sec.done("ent.stars");

    // Coverage contract: the scenario must keep exercising the branches.
    assert!(warp_seen, "replay never exercised the warp window");
    assert!(respawns_total >= 10, "replay never exercised edge respawns");
    assert!(expanded, "viewport growth never expanded the pool to 225");
    assert!(shrunk, "details drop never shrank the pool to 90");
    assert_eq!(sf.active_stars(), 90, "final density misfit");
}

// ---- Fleet bonus economy replay (Fatia 11) -----------------------------------

const FB_LEVEL: i32 = 1;
/// `GameRules::Progression::kWavesPerStage` -- the ctor-side max_level of the
/// dumper scenario; `level_data::get_total_levels()` is the same 15 and feeds
/// the session's internal rebuilds.
const FB_MAX_LEVEL: i32 = 15;
const FB_RNG_SEED: u32 = 0xFB1EED;
const FB_BULLETS_SEED: u32 = 0xFB0111;
const FB_BOMBS_SEED: u32 = 0xFB0222;
const FB_EXPLOSIONS_SEED: u32 = 0xFB0333;
const FB_CAP: i32 = 10000;
const FB_TAIL: i32 = 10;
/// The scored prelude: four mask bits recorded before wave 1, which leaves
/// the nuke (bit 5) as the only type the weighted draw can still choose --
/// spawn determinism without any player-side gating.
const FB_PRELUDE_TYPES: [i32; 4] = [1, 2, 3, 4];

/// Scenario-coverage flags; the seed search accepts a seed only when the full
/// economy arc fired -- the dumper's RunFleetBonusReplay gate, mirrored.
#[derive(Default)]
struct FbOutcome {
    kills: i32,
    spawns: i32,
    spawn_type: i32,
    rebuilds: i32,
    destroyed_frames: i32,
    capped_roll_frames: i32,
    capped_evidence_frame: i32,
    frames: i32,
    final_level: i32,
    final_count: i32,
    final_mask: i32,
}

impl FbOutcome {
    fn covered(&self) -> bool {
        self.spawns == 1
            && self.spawn_type == BonusType::ExtraNuke as i32
            && self.final_count == score::MAX_BONUSES_PER_STAGE
            && self.final_mask == 62
            && self.capped_roll_frames >= 1
            && self.rebuilds >= 1
            && self.final_level >= 2
            && self.frames < FB_CAP
    }
}

fn fb_replay(seed: u32, mut rec: Option<&mut Rows>) -> FbOutcome {
    let grid = formation::recompute(METRICS.scale);

    let mut bullets = BulletsManager::new(FB_BULLETS_SEED);
    let mut bombs = BulletsManager::new(FB_BOMBS_SEED);
    let mut bonuses = BonusManager::new();
    let mut explosions = ExplosionsManager::new(2, grid.width);
    explosions.seed_random(FB_EXPLOSIONS_SEED);
    bullets.set_metrics(METRICS);
    bombs.set_metrics(METRICS);
    bonuses.set_metrics(METRICS);
    explosions.set_metrics(METRICS.scale);
    let sink = EventSink::default();

    // The REAL level-1 catalog: the session arms every later wave from
    // level_data internally, so no synthetic table exists anywhere in this
    // scenario -- the economy rides the production wave pipeline end to end.
    let fleet = Fleet::new(
        FB_LEVEL,
        level_data::get_convoy_data(1).to_vec(),
        level_data::paths().to_vec(),
        METRICS,
        FB_MAX_LEVEL,
        seed,
    );
    let player = Player::new(METRICS);
    let lethal = grid.width as f32 * 1.4;

    // Flat 6.0 s fanfares: the hold formula is pinned by pt.hold; this
    // scenario pins the economy, so every hold is exactly 360 frames.
    let mut session = EntitySession::new(
        fleet,
        player,
        bullets,
        bombs,
        bonuses,
        explosions,
        sink,
        lethal,
        METRICS,
        FB_RNG_SEED,
        [6.0; 15],
    );

    // The scored prelude -- the dumper records the four types on the live
    // Score before the first frame.
    for t in FB_PRELUDE_TYPES {
        session.score_mut().record_bonus_spawned(t);
    }

    if let Some(sec) = rec.as_deref_mut() {
        let row = sec.next("ent.fb.setup");
        assert_eq!(u(&row, 1) as u32, seed, "setup fleet seed");
        assert_eq!(u(&row, 2) as u32, FB_RNG_SEED, "setup rng seed");
        assert_eq!(u(&row, 3) as u32, FB_BULLETS_SEED, "setup bullets seed");
        assert_eq!(u(&row, 4) as u32, FB_BOMBS_SEED, "setup bombs seed");
        assert_eq!(u(&row, 5) as u32, FB_EXPLOSIONS_SEED, "setup explosions seed");
        assert_eq!(i(&row, 6), METRICS.win_w, "setup win_w");
        assert_eq!(i(&row, 7), METRICS.win_h, "setup win_h");
        assert_eq!(hex32(&row[8]), METRICS.scale.to_bits(), "setup scale");
        assert_eq!(hex32(&row[9]), lethal.to_bits(), "setup lethal radius");
        assert_eq!(i(&row, 10), session.player().shield(), "setup initial shield");
        assert_eq!(i(&row, 11), session.score().level(), "setup level");
        assert_eq!(i(&row, 12), FB_CAP, "setup cap");
        assert_eq!(i(&row, 13), FB_TAIL, "setup tail");
        for (k, t) in FB_PRELUDE_TYPES.iter().enumerate() {
            let row = sec.next("ent.fb.prelude");
            assert_eq!(sz(&row, 1), k, "prelude idx");
            assert_eq!(i(&row, 2), *t, "prelude {k} type");
            assert_eq!(
                i(&row, 3),
                session.score().stage_bonuses_spawned_count(),
                "prelude {k} count"
            );
            assert_eq!(
                u(&row, 4) as u32,
                session.score().stage_bonus_mask(),
                "prelude {k} mask"
            );
        }
    }

    let mut out = FbOutcome { capped_evidence_frame: -1, ..FbOutcome::default() };
    let mut prev_count = session.score().stage_bonuses_spawned_count();
    let mut prev_mask = session.score().stage_bonus_mask();

    let mut f = 0i32;
    while f < FB_CAP {
        let level_before = session.score().level();
        // The stationary player shares one lane with the bullet column (same
        // discipline as ent.pt); destruction is report-only on both sides --
        // the harness never acts on the flag, the dead man keeps firing.
        let report = session.tick(0, true);
        out.kills += report.collisions.total_hits;
        if report.player_destroyed {
            out.destroyed_frames += 1;
        }

        // File order inside one frame: trigger, rebuild, spawn, frame, sink.
        if let Some(sec) = rec.as_deref_mut() {
            if report.transition_started {
                let row = sec.next("ent.fb.trigger");
                assert_eq!(i(&row, 1), f, "trigger frame");
                assert_eq!(i(&row, 2), level_before, "completed level");
                assert_eq!(i(&row, 3), report.transition_frames, "f{f} hold frames");
                assert_eq!(i(&row, 4), session.score().level(), "f{f} new level");
                assert_eq!(i(&row, 5), WARP_DURATION_FRAMES, "f{f} warp frames");
            }
        }
        if report.rebuilt {
            out.rebuilds += 1;
            if let Some(sec) = rec.as_deref_mut() {
                let row = sec.next("ent.fb.rebuild");
                assert_eq!(i(&row, 1), f, "rebuild frame");
                assert_eq!(i(&row, 2), report.rebuild_level, "f{f} rebuild level");
                assert_eq!(u(&row, 3) as u32, report.rebuild_seed, "f{f} rebuild seed");
                assert_eq!(
                    sz(&row, 4),
                    session.fleet().alien_count(),
                    "f{f} alive right after rebuild"
                );
                assert_eq!(
                    i(&row, 5),
                    stage_catalog::get_profile(report.rebuild_level, FB_MAX_LEVEL).convoy_count,
                    "f{f} rebuilt profile convoy_count"
                );
            }
        }

        // Spawn detection through the public economy: the count only grows
        // when the kill chain recorded a chosen type, and the mask delta
        // names it (highest new bit, mirroring the dumper's loop).
        let count = session.score().stage_bonuses_spawned_count();
        let mask = session.score().stage_bonus_mask();
        let mut spawned = false;
        if count > prev_count {
            spawned = true;
            out.spawns += 1;
            let new_bit = mask & !prev_mask;
            let mut kind = 0i32;
            for b in 1..=score::MAX_BONUSES_PER_STAGE {
                if new_bit & (1u32 << b) != 0 {
                    kind = b;
                }
            }
            out.spawn_type = kind;
            if let Some(sec) = rec.as_deref_mut() {
                let row = sec.next("ent.fb.spawn");
                assert_eq!(i(&row, 1), f, "spawn frame");
                assert_eq!(i(&row, 2), session.score().level(), "f{f} spawn wave");
                assert_eq!(i(&row, 3), kind, "f{f} spawn type");
                assert_eq!(i(&row, 4), count, "f{f} count after spawn");
                assert_eq!(u(&row, 5) as u32, mask, "f{f} mask after spawn");
            }
        }
        prev_count = count;
        prev_mask = mask;

        // Capped-roll evidence: the drop gate was open and the wait exhausted
        // on a killing frame with the economy at its cap -- a roll that
        // provably returned none WITHOUT consuming a draw.
        let capped_roll = session.fleet().bonus_allowed_this_level()
            && session.fleet().bonus_wait() <= 0
            && !session.fleet().bonus_spawned_this_level()
            && report.collisions.total_hits > 0
            && count >= score::MAX_BONUSES_PER_STAGE
            && !spawned;
        if capped_roll {
            out.capped_roll_frames += 1;
            if out.capped_evidence_frame < 0 {
                out.capped_evidence_frame = f;
            }
        }

        if let Some(sec) = rec.as_deref_mut() {
            // The dumper's sampling rule: every 60th frame plus every event
            // frame (kill, spawn, trigger, rebuild, capped roll).
            if f % 60 == 0
                || report.collisions.total_hits > 0
                || spawned
                || report.transition_started
                || report.rebuilt
                || capped_roll
            {
                let row = sec.next("ent.fb.frame");
                assert_eq!(i(&row, 1), f, "frame idx");
                assert_eq!(i(&row, 2), session.score().level(), "f{f} level");
                assert_eq!(sz(&row, 3), session.fleet().alien_count(), "f{f} alive");
                assert_eq!(i(&row, 4), session.player().shield(), "f{f} shield");
                assert_eq!(u(&row, 5), session.score().value(), "f{f} score");
                assert_eq!(i(&row, 6), session.bonuses().nb(), "f{f} pods");
                assert_eq!(i(&row, 7), count, "f{f} spawned count");
                assert_eq!(u(&row, 8) as u32, mask, "f{f} mask");
                assert_eq!(i(&row, 9), report.collisions.total_hits, "f{f} hits");
                assert_eq!(i(&row, 10), report.transition_remaining, "f{f} remaining");
                assert_eq!(
                    i(&row, 11),
                    i32::from(report.player_destroyed),
                    "f{f} destroyed"
                );
                assert_eq!(
                    i(&row, 12),
                    i32::from(report.transition_started),
                    "f{f} triggered"
                );
                assert_eq!(i(&row, 13), i32::from(report.rebuilt), "f{f} rebuilt");
                assert_eq!(
                    i(&row, 14),
                    i32::from(session.fleet().bonus_allowed_this_level()),
                    "f{f} allowed"
                );
                assert_eq!(i(&row, 15), session.fleet().bonus_wait(), "f{f} wait");
            }
            drain_sink_fb(session.sink_mut(), sec, "ent.fb.sink", f);
        } else {
            session.sink_mut().drain();
        }

        if out.capped_evidence_frame >= 0 && f - out.capped_evidence_frame >= FB_TAIL {
            f += 1;
            break;
        }
        f += 1;
    }

    out.frames = f;
    out.final_level = session.score().level();
    out.final_count = session.score().stage_bonuses_spawned_count();
    out.final_mask = session.score().stage_bonus_mask() as i32;
    if let Some(sec) = rec.as_deref_mut() {
        let row = sec.next("ent.fb.done");
        assert_eq!(i(&row, 1), out.frames, "done frames");
        assert_eq!(i(&row, 2), out.kills, "done kills");
        assert_eq!(i(&row, 3), out.spawns, "done spawns");
        assert_eq!(i(&row, 4), out.final_level, "done final level");
        assert_eq!(i(&row, 5), out.final_count, "done final count");
        assert_eq!(i(&row, 6), out.final_mask, "done final mask");
        assert_eq!(i(&row, 7), out.destroyed_frames, "done destroyed frames");
        assert_eq!(i(&row, 8), out.capped_roll_frames, "done capped frames");
        assert_eq!(i(&row, 9), out.rebuilds, "done rebuilds");
        sec.done("ent.fb");
    }
    out
}

/// The bonus-economy contract (RUST.md Fatia 11): with a live Score wired
/// into the kill chain, `RollSmartBonusType` honors the stage cap (no draw
/// consumed at 5 recorded types) and the cycle mask (recorded types are
/// zero-weight, so the prelude makes the wave-1 spawn deterministically the
/// nuke), `RecordBonusSpawned` lands before the pod drops, and `Rebuild`
/// keeps the mask while re-drawing wait+Bernoulli -- so the first capped roll
/// fires in the first post-spawn wave whose gate allows drops. The full arc
/// rides the REAL catalog (cd1 -> cd2 -> cd3) through two wave transitions,
/// a nuke pickup, and player destruction (report-only, no shell response).
#[test]
fn fleet_bonus_parity() {
    let rows = load_rows();

    // Deterministic seed search: the first seed whose economy arc completes
    // inside the budget -- the exact loop of DumpEntityFleetBonus.
    let mut chosen = 0u32;
    for s in 1u32..=256 {
        if fb_replay(s, None).covered() {
            chosen = s;
            break;
        }
    }
    assert_ne!(chosen, 0, "no fleet-bonus seed satisfied the coverage gate");

    let mut sec = section(&rows, "ent.fb.");
    let recorded = fb_replay(chosen, Some(&mut sec));
    assert!(recorded.covered(), "recorded replay lost coverage");
}

// ---- mf/hs/ent.mf: the match flow (Fatia 12) --------------------------------

/// Match-flow scenario inputs (the dumper's kMf* constants): the scripted
/// refresh rate, the Update() clock input, the pilot name and the two
/// replay budgets.
const MF_RATE: i32 = 60;
const MF_TS: i64 = 1_762_000_000;
const MF_PILOT: &str = "GOLDENPILOT";
const MF_NOVA_CAP: i32 = 3000;
const MF_DEATH_CAP: i32 = 6000;

/// Asserts one `hs.state` row (the pending ledger snapshot after an op).
fn mf_assert_state(hs: &HighScores, sec: &mut Rows, idx: i32) {
    let row = sec.next("hs.state");
    assert_eq!(i(&row, 1), idx, "state idx");
    assert_eq!(i(&row, 2), i32::from(hs.has_pending()), "state pending");
    assert_eq!(u(&row, 3), hs.last_value(), "state value");
    assert_eq!(i(&row, 4), hs.last_size().x, "state width");
    assert_eq!(i(&row, 5), hs.last_size().y, "state height");
    assert_eq!(i(&row, 6), hs.last_rate(), "state rate");
    assert_eq!(i(&row, 7), hs.last_stage(), "state stage");
    assert_eq!(i(&row, 8), hs.last_wave(), "state wave");
}

/// One scripted `Add` op of the hs.* ledger script: run, assert the op row
/// and the state row, advance the index.
fn mf_op_add(
    hs: &mut HighScores,
    sec: &mut Rows,
    idx: &mut i32,
    value: u64,
    size: Coord,
    rate: i32,
    stage: i32,
    wave: i32,
) {
    hs.add(value, size, rate, stage, wave);
    let row = sec.next("hs.op");
    assert_eq!(i(&row, 1), *idx, "op idx");
    assert_eq!(i(&row, 2), 0, "op kind: add");
    assert_eq!(u(&row, 3), value, "op value");
    assert_eq!(i(&row, 4), size.x, "op width");
    assert_eq!(i(&row, 5), size.y, "op height");
    assert_eq!(i(&row, 6), rate, "op rate");
    assert_eq!(i(&row, 7), stage, "op stage");
    assert_eq!(i(&row, 8), wave, "op wave");
    mf_assert_state(hs, sec, *idx);
    *idx += 1;
}

/// The Update() decision-core replay shared by every consume site (the hs.*
/// script and the two commit branches): feed the live table, assert the
/// entry row when something was consumed, then the update row -- including
/// the serialized-content length and its FNV-64, which pin the exact ledger
/// bytes.
fn mf_consume(
    hs: &mut HighScores,
    table: &mut HighScoreTable,
    sec: &mut Rows,
    tag: &str,
    idx: i32,
    ts: i64,
) {
    let res = hs.consume_pending(table, MF_PILOT, ts);
    if let Some((entry, _)) = &res {
        let row = sec.next(&format!("{tag}.entry"));
        assert_eq!(i(&row, 1), idx, "entry idx");
        assert_eq!(u(&row, 2), entry.value, "entry value");
        assert_eq!(row[3].parse::<i64>().unwrap(), entry.date, "entry date");
        assert_eq!(i(&row, 4), entry.width, "entry width");
        assert_eq!(i(&row, 5), entry.height, "entry height");
        assert_eq!(i(&row, 6), entry.refresh, "entry refresh");
        assert_eq!(i(&row, 7), entry.stage, "entry stage");
        assert_eq!(i(&row, 8), entry.wave, "entry wave");
        assert_eq!(row[9], entry.name, "entry name");
    }
    let row = sec.next(&format!("{tag}.update"));
    assert_eq!(i(&row, 1), idx, "update idx");
    assert_eq!(i(&row, 2), i32::from(res.is_some()), "update consumed");
    let added = match &res {
        Some((_, added)) => i32::from(*added),
        None => -1,
    };
    assert_eq!(i(&row, 3), added, "update added");
    assert_eq!(sz(&row, 4), table.all().len(), "update table count");
    let content = table.serialized_content();
    assert_eq!(sz(&row, 5), content.len(), "update content length");
    assert_eq!(u(&row, 6), content_hash(&content), "update content fnv64");
}

/// Builds a match-flow harness session: the real level catalog, the dumper's
/// manager seeds and the scale-1 metrics world. `pre_placed_bombs > 0`
/// mirrors the death scenario's scripted volley -- that many bombs stacked
/// above the player's real baseline (spacing 60, speed 6), placed before the
/// session adopts the manager.
fn mf_session(
    level: i32,
    seed: u32,
    bullets_seed: u32,
    bombs_seed: u32,
    explosions_seed: u32,
    pre_placed_bombs: i32,
) -> EntitySession {
    let grid = formation::recompute(METRICS.scale);
    let mut bullets = BulletsManager::new(bullets_seed);
    let mut bombs = BulletsManager::new(bombs_seed);
    let mut bonuses = BonusManager::new();
    let mut explosions = ExplosionsManager::new(2, grid.width);
    explosions.seed_random(explosions_seed);
    bullets.set_metrics(METRICS);
    bombs.set_metrics(METRICS);
    bonuses.set_metrics(METRICS);
    explosions.set_metrics(METRICS.scale);
    let fleet = Fleet::new(
        level,
        level_data::get_convoy_data(level as usize).to_vec(),
        level_data::paths().to_vec(),
        METRICS,
        15, // GameRules::Progression::kWavesPerStage -- the dumper's argument
        seed,
    );
    let player = Player::new(METRICS);
    if pre_placed_bombs > 0 {
        let player_y = METRICS.win_h - PLAYER_BOTTOM_MARGIN_PX - player.height() / 2;
        for k in 0..pre_placed_bombs {
            bombs.add(
                TextureId::Bomb,
                Coord::new(METRICS.win_w / 2, player_y - 60 - k * 60),
                Coord::new(0, 6),
                false,
                false,
                0,
                0.0,
                false,
                SEEKER_ENGINE_COLOR,
            );
        }
    }
    let lethal = grid.width as f32 * 1.4;
    let sink = EventSink::default();
    EntitySession::new(
        fleet, player, bullets, bombs, bonuses, explosions, sink, lethal, METRICS, 0, [6.0; 15],
    )
}

/// Fatia 12: the match flow -- the overlay/cinematic math, the HighScores
/// pending ledger script, the cheat response and the two live end-of-run
/// scenarios (the kamikaze supernova with a committed score, and the
/// scripted-bomb destruction with the CancelPending path). Every row is
/// bit-exact; both live replays ride the production-faithful tick halves
/// (`tick_collisions` ends the frame at the kamikaze probe, `tick_motion`
/// only runs on frames production would move).
#[test]
fn match_flow_parity() {
    let rows = load_rows();

    // --- pure math: the overlay curve and every refresh-derived count.
    let mut sec = section(&rows, "mf.");
    for case in 0..9i32 {
        let row = sec.next("mf.overlay");
        assert_eq!(i(&row, 1), case, "overlay case idx");
        let p = hb(&row[2]);
        assert_eq!(
            hex32(&row[3]),
            overlay_alpha_at(p).to_bits(),
            "overlay alpha case {case}"
        );
    }
    for _ in 0..8 {
        for _ in 0..4 {
            let row = sec.next("mf.cinematic");
            let rate = i(&row, 1);
            let sx100 = i(&row, 2);
            assert_eq!(
                cinematic_frames(rate, f64::from(sx100) / 100.0),
                i(&row, 3),
                "cinematic frames rate {rate} sx100 {sx100}"
            );
        }
        let row = sec.next("mf.skip");
        let rate = i(&row, 1);
        let nova = cinematic_frames(rate, NOVA_CINEMATIC_SECONDS);
        let death = cinematic_frames(rate, DESTRUCTION_CINEMATIC_SECONDS);
        assert_eq!(i(&row, 2), nova, "skip nova frames");
        assert_eq!(i(&row, 3), nova / NOVA_SKIP_DIVISOR, "skip nova interval");
        assert_eq!(i(&row, 4), death, "skip death frames");
        assert_eq!(
            i(&row, 5),
            death / DESTRUCTION_SKIP_DIVISOR,
            "skip death interval"
        );
        let row = sec.next("mf.osd");
        assert_eq!(i(&row, 1), rate, "osd rate");
        assert_eq!(details_osd_frames(rate), i(&row, 2), "osd frames");
    }
    sec.done("mf");

    // --- the HighScores pending ledger script: the score==0 guard, the
    // --- cancel residue, the consume-into-table core and the exact
    // --- duplicate rejection.
    let mut sec = section(&rows, "hs.");
    let mut hs = HighScores::new();
    let mut table = HighScoreTable::new();
    let mut idx = 0i32;
    mf_op_add(&mut hs, &mut sec, &mut idx, 0, Coord::new(1280, 720), 60, 1, 1);
    mf_op_add(&mut hs, &mut sec, &mut idx, 130, Coord::new(1280, 720), 60, 1, 2);
    {
        let row = sec.next("hs.op");
        assert_eq!(i(&row, 1), idx, "cancel op idx");
        assert_eq!(i(&row, 2), 1, "op kind: cancel");
        hs.cancel_pending();
        mf_assert_state(&hs, &mut sec, idx);
        idx += 1;
    }
    mf_op_add(&mut hs, &mut sec, &mut idx, 2500, Coord::new(1920, 1080), 144, 3, 7);
    mf_consume(&mut hs, &mut table, &mut sec, "hs", idx, MF_TS);
    idx += 1;
    mf_consume(&mut hs, &mut table, &mut sec, "hs", idx, MF_TS + 1);
    idx += 1;
    mf_op_add(&mut hs, &mut sec, &mut idx, 2500, Coord::new(1920, 1080), 144, 3, 7);
    mf_consume(&mut hs, &mut table, &mut sec, "hs", idx, MF_TS);
    idx += 1;
    mf_consume(&mut hs, &mut table, &mut sec, "hs", idx, MF_TS + 7);
    sec.done("hs");

    // --- the cheat-input response: two stacking passes on a live session
    // --- (production has no re-apply guard).
    let mut sec = section(&rows, "ent.mf.cheat");
    let mut session = mf_session(1, 1, 0x4D460111, 0x4D460222, 0x4D460333, 0);
    session.highscores_mut().add(999, Coord::new(1280, 720), 60, 2, 5);
    for phase in 0..3i32 {
        if phase > 0 {
            session.apply_cheat();
        }
        let row = sec.next("ent.mf.cheat");
        assert_eq!(i(&row, 1), phase, "cheat phase");
        assert_eq!(i(&row, 2), session.player().fire_level(), "fire level");
        assert_eq!(i(&row, 3), session.player().multi_fire(), "multi fire");
        assert_eq!(i(&row, 4), session.player().shield(), "shield");
        assert_eq!(i(&row, 5), i32::from(session.score().cheated()), "cheated");
        assert_eq!(
            i(&row, 6),
            i32::from(session.highscores().has_pending()),
            "pending"
        );
        assert_eq!(
            u(&row, 7),
            session.highscores().last_value(),
            "pending value"
        );
    }
    sec.done("ent.mf.cheat");

    // --- live nova run: level 3, stationary firing player; the kamikaze
    // --- diver wins the race and the production response fires.
    let mut sec = section(&rows, "ent.mf.nova");
    let row = sec.next("ent.mf.nova.setup");
    let seed = u(&row, 1) as u32;
    assert_eq!(u(&row, 2), 0x4D461111, "bullets seed");
    assert_eq!(u(&row, 3), 0x4D462222, "bombs seed");
    assert_eq!(u(&row, 4), 0x4D463333, "explosions seed");
    assert_eq!(i(&row, 5), METRICS.win_w, "win w");
    assert_eq!(i(&row, 6), METRICS.win_h, "win h");
    assert_eq!(hex32(&row[7]), METRICS.scale.to_bits(), "scale bits");
    let lethal = formation::recompute(METRICS.scale).width as f32 * 1.4;
    assert_eq!(hex32(&row[8]), lethal.to_bits(), "lethal bits");
    let mut session = mf_session(3, seed, 0x4D461111, 0x4D462222, 0x4D463333, 0);
    assert_eq!(i(&row, 9), session.player().shield(), "initial shield");
    assert_eq!(i(&row, 10), 3, "level");
    assert_eq!(i(&row, 11), MF_NOVA_CAP, "cap");

    let mut f = 0i32;
    let mut kills = 0i32;
    let prox_frame = loop {
        let report = session.tick_collisions(false);
        if report.kamikaze_proximity {
            // Production returns HERE: the frame's bonus collisions, shield
            // sync and every Move are replaced by the supernova branch.
            let row = sec.next("ent.mf.nova.prox");
            assert_eq!(i(&row, 1), f, "prox frame");
            assert_eq!(sz(&row, 2), session.fleet().alien_count(), "prox aliens");
            assert_eq!(i(&row, 3), session.player().shield(), "prox shield");
            assert_eq!(u(&row, 4), session.score().value(), "prox score");
            assert_eq!(
                sz(&row, 5),
                session.explosions().active_count(),
                "prox explosions"
            );
            drain_sink_fb(session.sink_mut(), &mut sec, "ent.mf.nova.sink", f);
            break f;
        }
        // Scenario gates: the diver must beat both the bomb economy and the
        // wave clear (the dumper searched its seed for exactly this outcome).
        assert!(!report.player_destroyed, "nova gate: bomb death at f{f}");
        assert!(!session.fleet().finished(), "nova gate: trigger at f{f}");
        session.tick_motion(0, true);
        kills += report.collisions.total_hits;
        if f % 20 == 0 || report.collisions.total_hits > 0 || report.bomb_shield_loss {
            let row = sec.next("ent.mf.nova.frame");
            assert_eq!(i(&row, 1), f, "frame stamp");
            assert_eq!(sz(&row, 2), session.fleet().alien_count(), "aliens");
            assert_eq!(i(&row, 3), session.player().shield(), "shield");
            assert_eq!(u(&row, 4), session.score().value(), "score");
            assert_eq!(i(&row, 5), report.collisions.total_hits, "hits");
            assert_eq!(i(&row, 6), i32::from(report.bomb_shield_loss), "bomb loss");
            assert_eq!(
                sz(&row, 7),
                session.explosions().active_count(),
                "explosions"
            );
        }
        drain_sink_fb(session.sink_mut(), &mut sec, "ent.mf.nova.sink", f);
        f += 1;
        assert!(f < MF_NOVA_CAP, "nova cap exhausted");
    };

    // The kamikaze branch: score mirror to -1, guarded commit (the Add path
    // -- this run scored), the screen-wide burst, then the 3 s
    // explosions-only cinematic.
    let committed = session.kamikaze_supernova_response(MF_RATE);
    assert!(committed, "nova run must commit");
    assert!(session.score().value() > 0, "nova gate: positive score");
    let row = sec.next("ent.mf.nova.commit");
    assert_eq!(i(&row, 1), 1, "committed");
    assert_eq!(u(&row, 2), session.score().value(), "commit value");
    assert_eq!(i(&row, 3), session.max_session_size().x, "commit width");
    assert_eq!(i(&row, 4), session.max_session_size().y, "commit height");
    assert_eq!(i(&row, 5), MF_RATE, "commit rate");
    assert_eq!(i(&row, 6), session.score().cycle(), "commit cycle");
    assert_eq!(i(&row, 7), session.score().level(), "commit level");
    let mut table = HighScoreTable::new();
    mf_consume(
        session.highscores_mut(),
        &mut table,
        &mut sec,
        "ent.mf.nova.commit",
        0,
        MF_TS,
    );
    let row = sec.next("ent.mf.nova.burst");
    assert_eq!(i(&row, 1), prox_frame, "burst frame");
    assert_eq!(
        sz(&row, 2),
        session.explosions().active_count(),
        "burst active"
    );
    let nova_frames = cinematic_frames(MF_RATE, NOVA_CINEMATIC_SECONDS);
    assert_eq!(i(&row, 3), nova_frames, "nova frames");
    assert_eq!(i(&row, 4), nova_frames / NOVA_SKIP_DIVISOR, "skip after");
    for k in 0..nova_frames {
        session.supernova_cinematic_tick();
        if k == 30 {
            // Mid-cinematic particle sample (the burst decays to zero by
            // k~80): pins nova spawn physics beyond the per-frame counts.
            sample_particles(
                session.explosions(),
                &mut sec,
                "ent.mf.nova.cine",
                "ent.mf.nova.p",
                k,
                4,
            );
        } else if k % 10 == 0 {
            let row = sec.next("ent.mf.nova.cine");
            assert_eq!(i(&row, 1), k, "cine frame");
            assert_eq!(
                sz(&row, 2),
                session.explosions().active_count(),
                "cine active"
            );
        }
    }
    let row = sec.next("ent.mf.nova.debrief");
    assert_eq!(i(&row, 1), i32::from(committed), "debrief committed");
    assert_eq!(
        i(&row, 2),
        i32::from(session.debrief_new_record()),
        "new record"
    );
    assert_eq!(i(&row, 3), session.debrief_status_kind(), "status kind");
    assert_eq!(sz(&row, 4), table.all().len(), "table count");
    assert_eq!(
        i(&row, 5),
        cinematic_frames(MF_RATE, GAME_OVER_SECONDS),
        "game over frames"
    );
    let row = sec.next("ent.mf.nova.done");
    assert_eq!(i(&row, 1), prox_frame, "done frames");
    assert_eq!(i(&row, 2), kills, "done kills");
    assert_eq!(u(&row, 3), session.score().value(), "done score");
    assert_eq!(i(&row, 4), i32::from(committed), "done committed");
    assert_eq!(
        i(&row, 5),
        i32::from(session.highscores().has_pending()),
        "done pending"
    );
    sec.done("ent.mf.nova");

    // --- live death run: level 1, stationary NON-firing player, four
    // --- scripted bombs; the fourth hit drops the shield below zero and the
    // --- destruction branch fires with the CancelPending commit path.
    let mut sec = section(&rows, "ent.mf.death");
    let row = sec.next("ent.mf.death.setup");
    let seed = u(&row, 1) as u32;
    assert_eq!(u(&row, 2), 0x4D464444, "bullets seed");
    assert_eq!(u(&row, 3), 0x4D465555, "bombs seed");
    assert_eq!(u(&row, 4), 0x4D466666, "explosions seed");
    assert_eq!(i(&row, 5), METRICS.win_w, "win w");
    assert_eq!(i(&row, 6), METRICS.win_h, "win h");
    assert_eq!(hex32(&row[7]), METRICS.scale.to_bits(), "scale bits");
    let lethal = formation::recompute(METRICS.scale).width as f32 * 1.4;
    assert_eq!(hex32(&row[8]), lethal.to_bits(), "lethal bits");
    let mut session = mf_session(1, seed, 0x4D464444, 0x4D465555, 0x4D466666, 4);
    assert_eq!(i(&row, 9), session.player().shield(), "initial shield");
    assert_eq!(i(&row, 10), 1, "level");
    assert_eq!(i(&row, 11), MF_DEATH_CAP, "cap");

    let mut f = 0i32;
    let death_frame = loop {
        let report = session.tick_collisions(false);
        assert!(
            !report.kamikaze_proximity,
            "death gate: diver arrived first at f{f}"
        );
        if report.player_destroyed {
            let row = sec.next("ent.mf.death.hit");
            assert_eq!(i(&row, 1), f, "hit frame");
            assert_eq!(sz(&row, 2), session.fleet().alien_count(), "hit aliens");
            assert_eq!(i(&row, 3), session.player().shield(), "hit shield");
            assert_eq!(
                sz(&row, 4),
                session.explosions().active_count(),
                "hit explosions"
            );
            assert_eq!(i(&row, 5), session.bombs().nb(), "hit bombs");
            drain_sink_fb(session.sink_mut(), &mut sec, "ent.mf.death.sink", f);
            break f;
        }
        assert!(!session.fleet().finished(), "death gate: trigger at f{f}");
        session.tick_motion(0, false);
        if f % 60 == 0 || report.bomb_shield_loss {
            let row = sec.next("ent.mf.death.frame");
            assert_eq!(i(&row, 1), f, "frame stamp");
            assert_eq!(sz(&row, 2), session.fleet().alien_count(), "aliens");
            assert_eq!(i(&row, 3), session.player().shield(), "shield");
            assert_eq!(u(&row, 4), session.score().value(), "score");
            assert_eq!(i(&row, 5), i32::from(report.bomb_shield_loss), "bomb loss");
            assert_eq!(i(&row, 6), session.bombs().nb(), "bombs");
            assert_eq!(
                sz(&row, 7),
                session.explosions().active_count(),
                "explosions"
            );
        }
        drain_sink_fb(session.sink_mut(), &mut sec, "ent.mf.death.sink", f);
        f += 1;
        assert!(f < MF_DEATH_CAP, "death cap exhausted");
    };

    // The destruction branch: the guarded commit takes the CancelPending
    // path (the pilot never scored), then the two hull bursts and the 1.35 s
    // four-manager cinematic (the armada only draws in production).
    let committed = session.grand_destruction_response(MF_RATE);
    assert!(!committed, "death run must not commit");
    let row = sec.next("ent.mf.death.commit");
    assert_eq!(i(&row, 1), 0, "commit kind: cancel");
    assert_eq!(u(&row, 2), session.score().value(), "commit value");
    assert_eq!(
        i(&row, 3),
        i32::from(session.highscores().has_pending()),
        "pending after cancel"
    );
    assert_eq!(
        u(&row, 4),
        session.highscores().last_value(),
        "ledger value after cancel"
    );
    let row = sec.next("ent.mf.death.burst");
    assert_eq!(i(&row, 1), death_frame, "burst frame");
    assert_eq!(
        sz(&row, 2),
        session.explosions().active_count(),
        "burst active"
    );
    let death_frames = cinematic_frames(MF_RATE, DESTRUCTION_CINEMATIC_SECONDS);
    assert_eq!(i(&row, 3), death_frames, "death frames");
    assert_eq!(
        i(&row, 4),
        death_frames / DESTRUCTION_SKIP_DIVISOR,
        "skip after"
    );
    for k in 0..death_frames {
        session.destruction_cinematic_tick();
        if k % 10 == 0 {
            let row = sec.next("ent.mf.death.cine");
            assert_eq!(i(&row, 1), k, "cine frame");
            assert_eq!(i(&row, 2), session.bonuses().nb(), "cine bonuses");
            assert_eq!(i(&row, 3), session.bullets().nb(), "cine bullets");
            assert_eq!(i(&row, 4), session.bombs().nb(), "cine bombs");
            assert_eq!(
                sz(&row, 5),
                session.explosions().active_count(),
                "cine explosions"
            );
        }
    }
    let row = sec.next("ent.mf.death.debrief");
    assert_eq!(i(&row, 1), i32::from(committed), "debrief committed");
    assert_eq!(
        i(&row, 2),
        i32::from(session.debrief_new_record()),
        "new record"
    );
    assert_eq!(i(&row, 3), session.debrief_status_kind(), "status kind");
    assert_eq!(sz(&row, 4), 0, "table count (never fed)");
    assert_eq!(
        i(&row, 5),
        cinematic_frames(MF_RATE, GAME_OVER_SECONDS),
        "game over frames"
    );
    let row = sec.next("ent.mf.death.done");
    assert_eq!(i(&row, 1), f, "done frames");
    assert_eq!(i(&row, 2), death_frame, "done death frame");
    assert_eq!(u(&row, 3), session.score().value(), "done score");
    assert_eq!(i(&row, 4), i32::from(committed), "done committed");
    assert_eq!(
        i(&row, 5),
        i32::from(session.highscores().has_pending()),
        "done pending"
    );
    sec.done("ent.mf.death");
}
