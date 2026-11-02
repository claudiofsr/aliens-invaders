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
    mathx::Coord,
    path::{self, Vec2f},
    player::Player,
    sink::{EventSink, PresentationEvent},
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
    player.do_bonus_collisions(bonuses, explosions, sink, None);
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

/// One seeded fleet replay in the EXACT production tick order. With
/// `rec = Some` it asserts every corpus row of the recorded replay.
fn fleet_replay(seed: u32, mut rec: Option<&mut Rows>) -> FleetOutcome {
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
    let mut sink = EventSink::default();

    let mut fleet = Fleet::new(
        FLEET_LEVEL,
        convoys.clone(),
        paths.clone(),
        METRICS,
        FLEET_MAX_LEVEL,
        seed,
    );
    let mut player = Player::new(METRICS);

    // Kamikaze blast radius: the fleet grid width times the SSOT multiplier.
    let lethal = grid.width as f32 * 1.4;

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

    let mut out = FleetOutcome::default();
    let mut score_total = 0u64;
    let mut script = InputScript { state: 0x1234_ABCD };

    let mut f = 0i32;
    while f < FLEET_REPLAY_FRAMES {
        let dir = script.step();

        // Exact production tick order (CombatSession::TickFixedPhysics):
        // collisions first, then bonuses/player/projectiles, fleet last.
        let summary = fleet.do_bullets_collisions(
            &mut bullets, &mut explosions, &mut bonuses, &mut sink, Some(&player),
        );
        player.do_bombs_collisions(&mut bombs, &mut explosions, &mut sink);
        let prox = fleet.check_kamikaze_proximity(player.position(), lethal);
        player.do_bonus_collisions(
            &mut bonuses, &mut explosions, &mut sink, Some(&mut fleet),
        );
        bonuses.advance();
        player.move_player(dir);
        bullets.advance(Coord::new(-1, -1));
        bombs.advance(player.position());
        player.fire(&mut bullets, &mut sink);
        explosions.advance();
        fleet.advance(&bullets, &mut sink, Some(player.position()));
        fleet.fire(player.position(), &mut bombs);

        score_total += summary.score_earned;
        out.kills += summary.total_hits;
        if bonuses.nb() > 0
            || player.speed_level() + player.fire_level() + player.multi_fire() > 1
        {
            out.bonus_spawned = true;
        }
        for a in 0..fleet.alien_count() {
            let alien = fleet.get_alien(a);
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
            assert_eq!(sz(&row, 4), fleet.alien_count(), "f{f} aliens");
            assert_eq!(i(&row, 5), bombs.nb(), "f{f} bombs");
            assert_eq!(i(&row, 6), bullets.nb(), "f{f} bullets");
            assert_eq!(i(&row, 7), bonuses.nb(), "f{f} bonuses");
            assert_eq!(sz(&row, 8), explosions.active_count(), "f{f} explosions");
            assert_eq!(i(&row, 9), player.position().x, "f{f} pl_x");
            assert_eq!(i(&row, 10), player.position().y, "f{f} pl_y");
            assert_eq!(i(&row, 11), player.shield(), "f{f} shield");
            assert_eq!(i(&row, 12), player.fire_level(), "f{f} fire_lvl");
            assert_eq!(i(&row, 13), player.multi_fire(), "f{f} multi");
            assert_eq!(i(&row, 14), player.speed_level(), "f{f} speed_lvl");
            assert_eq!(i(&row, 15), summary.total_hits, "f{f} hits");
            assert_eq!(i(&row, 16), summary.regular_hits, "f{f} regular");
            assert_eq!(i(&row, 17), summary.kamikaze_hits, "f{f} kamikaze");
            assert_eq!(u(&row, 18), score_total, "f{f} score");
            assert_eq!(i(&row, 19), i32::from(prox), "f{f} proximity");

            for a in 0..fleet.alien_count() {
                let alien = fleet.get_alien(a);
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
            for a in 0..bombs.nb() as usize {
                let b = bombs.get_active(a);
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
            for a in 0..bullets.nb() as usize {
                let b = bullets.get_active(a);
                let row = sec.next("ent.fleet.bullet");
                assert_eq!(i(&row, 1), f, "bullet frame");
                assert_eq!(i(&row, 2), a as i32, "bullet idx");
                assert_eq!(i(&row, 3), b.pos.x, "f{f} bullet{a} x");
                assert_eq!(i(&row, 4), b.pos.y, "f{f} bullet{a} y");
                assert_eq!(i(&row, 5), b.speed.x, "f{f} bullet{a} vx");
                assert_eq!(i(&row, 6), b.speed.y, "f{f} bullet{a} vy");
            }
            for a in 0..bonuses.nb() as usize {
                let s = bonuses.get_active(a);
                let row = sec.next("ent.fleet.bonus");
                assert_eq!(i(&row, 1), f, "bonus frame");
                assert_eq!(i(&row, 2), a as i32, "bonus idx");
                assert_eq!(i(&row, 3), s.bonus_type as i32, "f{f} bonus{a} type");
                assert_eq!(i(&row, 4), s.pos.x, "f{f} bonus{a} x");
                assert_eq!(i(&row, 5), s.pos.y, "f{f} bonus{a} y");
            }
            drain_sink(&mut sink, sec, "ent.fleet.sink", f);
        } else {
            sink.drain();
        }

        // Early exit: armada finished and every pool drained (only after the
        // search had a fair combat window for the coverage contract).
        if f >= 500
            && fleet.finished()
            && bombs.nb() == 0
            && bullets.nb() == 0
            && bonuses.nb() == 0
            && explosions.active_count() == 0
        {
            f += 1;
            break;
        }
        f += 1;
    }

    out.frames = f;
    out.finished = fleet.finished();
    if let Some(sec) = rec.as_deref_mut() {
        let row = sec.next("ent.fleet.done");
        assert_eq!(i(&row, 1), out.frames, "done frames");
        assert_eq!(i(&row, 2), i32::from(out.finished), "done finished");
        assert_eq!(i(&row, 3), out.kills, "done kills");
        sec.done("ent.fleet");
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
        let probe = fleet_replay(s, None);
        if probe.coverage() {
            chosen = s;
            break;
        }
    }
    assert_ne!(chosen, 0, "no fleet seed satisfied the coverage contract");

    let mut sec = section(&rows, "ent.fleet.");
    let recorded = fleet_replay(chosen, Some(&mut sec));
    assert!(recorded.coverage(), "recorded replay lost coverage");
}
