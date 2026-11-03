//! Golden-vector parity (RUST.md Phase 1/2a): every covered line of
//! golden/vectors-cpp.txt must match the Rust port BIT FOR BIT. Sections of
//! later phases (profile, fleet.*, player.*, score_hash, serialize) are
//! counted to prove the corpus parses, but not asserted yet.

use kernel::{
    formation, highscore, level_data, mathx, pacing,
    path::{self, Vec2f},
    random_stream::RandomStream,
    score, score_hash, stage_catalog,
    trajectory::{Trajectory, ViewportMetrics},
};
use std::collections::BTreeMap;
use std::path::PathBuf;

fn load_vectors() -> Vec<Vec<String>> {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../golden/vectors-cpp.txt");
    let text = std::fs::read_to_string(&path).unwrap_or_else(|e| {
        panic!(
            "golden vectors missing ({}): {}\nGenerate them first:\n  \
             cmake -B build -DBUILD_TESTS=OFF && cmake --build build --target dump_vectors\n  \
             ./build/dump_vectors > golden/vectors-cpp.txt",
            path.display(),
            e
        )
    });
    text.lines()
        .filter(|l| !l.starts_with('#') && !l.trim().is_empty())
        .map(|l| l.split_whitespace().map(str::to_string).collect())
        .collect()
}

fn group(rows: &[Vec<String>]) -> BTreeMap<String, Vec<Vec<String>>> {
    let mut map: BTreeMap<String, Vec<Vec<String>>> = BTreeMap::new();
    for row in rows {
        map.entry(row[0].clone()).or_default().push(row.clone());
    }
    map
}

fn hex32(tok: &str) -> u32 {
    u32::from_str_radix(tok, 16).unwrap_or_else(|_| panic!("bad hex32 token: {tok}"))
}

fn hex64(tok: &str) -> u64 {
    u64::from_str_radix(tok, 16).unwrap_or_else(|_| panic!("bad hex64 token: {tok}"))
}

fn i(row: &[String], idx: usize) -> i32 {
    row[idx].parse().unwrap_or_else(|_| panic!("bad int token: {}", row[idx]))
}

fn u(row: &[String], idx: usize) -> u64 {
    row[idx].parse().unwrap_or_else(|_| panic!("bad u64 token: {}", row[idx]))
}

#[test]
fn rng_parity() {
    let rows = load_vectors();
    let g = group(&rows);

    let seq = &g["rng.u32"];
    assert_eq!(seq.len(), 64);
    let mut rng = RandomStream::new(12345);
    for (n, row) in seq.iter().enumerate() {
        assert_eq!(i(row, 1), n as i32);
        assert_eq!(u(row, 2) as u32, rng.next_u32(), "rng.u32 draw {n}");
    }

    // One sequential stream, exact draw order (the C++ contract).
    let mut rng = RandomStream::new(999);
    for row in &g["rng.uniform_int"] {
        let got = rng.uniform_int(-50, 49);
        assert_eq!(i(row, 2), got, "uniform_int draw {}", row[1]);
    }
    for row in &g["rng.uniform_float"] {
        let got = rng.uniform_float(0.0, 1.0);
        assert_eq!(hex32(&row[2]), got.to_bits(), "uniform_float draw {}", row[1]);
    }
    for row in &g["rng.bernoulli"] {
        let got = rng.bernoulli(0.30);
        assert_eq!(i(row, 2), i32::from(got), "bernoulli draw {}", row[1]);
    }
    for row in &g["rng.chance"] {
        let got = rng.chance(1, 140);
        assert_eq!(i(row, 2), i32::from(got), "chance draw {}", row[1]);
    }

    let zero = RandomStream::new(0);
    let mut zero = zero;
    assert_eq!(u(&g["rng.seed0_first"][0], 1) as u32, zero.next_u32());
}

#[test]
fn math_parity() {
    let rows = load_vectors();
    let g = group(&rows);
    for row in &g["math.fast_round"] {
        let v = f32::from_bits(hex32(&row[1]));
        assert_eq!(i(row, 2), mathx::fast_round_f32(v), "fast_round({v})");
    }
    for row in &g["math.floor_div"] {
        let (a, b) = (i(row, 1), i(row, 2));
        assert_eq!(i(row, 3), mathx::floor_div(a, b), "floor_div({a},{b})");
    }
    for row in &g["math.chebyshev"] {
        let (x1, y1, x2, y2) = (i(row, 1), i(row, 2), i(row, 3), i(row, 4));
        assert_eq!(
            i(row, 5),
            mathx::chebyshev(x1, y1, x2, y2),
            "chebyshev({x1},{y1},{x2},{y2})"
        );
    }
}

#[test]
fn formation_parity() {
    let rows = load_vectors();
    let g = group(&rows);

    let grid = formation::unit_grid();
    assert_eq!(g["unit_grid"].len(), 5 * 12 * 16);
    for row in &g["unit_grid"] {
        let (m, r, c) = (i(row, 1) as usize, i(row, 2) as usize, i(row, 3) as usize);
        let u = &grid[m][r][c];
        assert_eq!(hex32(&row[4]), u.offset_x.to_bits(), "unit_grid x [{m}][{r}][{c}]");
        assert_eq!(hex32(&row[5]), u.offset_y.to_bits(), "unit_grid y [{m}][{r}][{c}]");
    }

    // Scales dumped in this exact order by the C++ tool.
    let scales = [1.0f32, 1.75, 0.5];
    let states: Vec<formation::GridData> = scales.iter().map(|s| formation::recompute(*s)).collect();

    assert_eq!(g["slots.metrics"].len(), 3);
    for row in &g["slots.metrics"] {
        let s = i(row, 1) as usize;
        let d = &states[s];
        assert_eq!(
            (i(row, 2), i(row, 3), i(row, 4), i(row, 5), i(row, 6)),
            (d.width, d.height, d.spacing_x, d.spacing_y, d.base_cruise_y),
            "slots.metrics scale {s}"
        );
    }

    assert_eq!(g["slots"].len(), 3 * 5 * 12 * 16);
    for row in &g["slots"] {
        let (s, m, r, c) = (
            i(row, 1) as usize,
            i(row, 2),
            i(row, 3),
            i(row, 4),
        );
        let slot = formation::get_slot(&states[s], m + 1, r, c);
        assert_eq!((i(row, 5), i(row, 6)), (slot.offset_x, slot.offset_y),
                   "slots s{s} m{m} r{r} c{c}");
    }

    // Edges were dumped with the LAST scale active (0.5).
    let last = states.last().unwrap();
    let lo = formation::get_slot(last, 0, -5, -5);
    let hi = formation::get_slot(last, 999, 999, 999);
    let low_row = g["slot_edge"].iter().find(|r| r[1] == "low").unwrap();
    let high_row = g["slot_edge"].iter().find(|r| r[1] == "high").unwrap();
    assert_eq!((i(low_row, 2), i(low_row, 3)), (lo.offset_x, lo.offset_y));
    assert_eq!((i(high_row, 2), i(high_row, 3)), (hi.offset_x, hi.offset_y));
}

#[test]
fn pacing_parity() {
    let rows = load_vectors();
    let g = group(&rows);

    let deltas: [u64; 9] = [
        16_666_667, 16_666_667, 50_000_000, 300_000_000, 8_000_000,
        20_000_000, 16_000_000, 17_000_000, 49_999_999,
    ];
    let mut acc = 0u64;
    assert_eq!(g["pacing.step"].len(), 9);
    for row in &g["pacing.step"] {
        let n = i(row, 1) as usize;
        let raw = deltas[n];
        assert_eq!(u(row, 2), raw);
        acc = pacing::accumulate_bounded(acc, raw);
        assert_eq!(i(row, 3), i32::from(pacing::is_hitch(raw)), "is_hitch step {n}");
        assert_eq!(u(row, 4), pacing::simulated_delta(raw), "simulated step {n}");
        assert_eq!(u(row, 5), acc, "accumulator step {n}");
    }

    for row in &g["pacing.threshold"] {
        let steps = i(row, 1);
        assert_eq!(u(row, 2), pacing::step_threshold(steps));
    }
    for row in &g["pacing.alpha"] {
        let a = u(row, 1);
        assert_eq!(
            hex32(&row[2]),
            pacing::interpolation_alpha(a).to_bits(),
            "alpha({a})"
        );
    }
    for row in &g["pacing.consume"] {
        assert_eq!(u(row, 2), pacing::consume_step(u(row, 1)));
    }
}

#[test]
fn catalog_parity() {
    let rows = load_vectors();
    let g = group(&rows);

    assert_eq!(g["profile"].len(), 75);
    for row in &g["profile"] {
        let w = i(row, 1);
        let p = stage_catalog::get_profile(w, 15);
        assert_eq!(i(row, 2), p.stage_cycle, "profile {w} stage_cycle");
        assert_eq!(i(row, 3), p.convoy_count, "profile {w} convoy_count");
        assert_eq!(i(row, 4), p.max_bombs, "profile {w} max_bombs");
        assert_eq!(hex32(&row[5]), p.cycle_bomb_mult.to_bits(), "profile {w} bomb_mult");
        assert_eq!(hex32(&row[6]), p.fleet_speed.to_bits(), "profile {w} fleet_speed");
        assert_eq!(i(row, 7), p.max_attack_wait_frames, "profile {w} wait");
        assert_eq!(i(row, 8), p.kamikaze_quota, "profile {w} kamikaze");
        assert_eq!(i(row, 9), i32::from(p.is_radioactive_wave), "profile {w} radioactive");
        assert_eq!(i(row, 10), i32::from(p.is_relativistic_wave), "profile {w} relativistic");
        assert_eq!(i(row, 11), i32::from(p.wanderers_allowed), "profile {w} wanderers");
    }

    for row in &g["fleet.speed"] {
        let lvl = i(row, 1);
        assert_eq!(
            hex32(&row[2]),
            stage_catalog::fleet_compute_speed(lvl, 15).to_bits(),
            "fleet.speed({lvl})"
        );
    }
    for row in &g["fleet.attack_wait"] {
        assert_eq!(i(row, 2), stage_catalog::get_max_attack_wait_frames(i(row, 1)));
    }
    for row in &g["fleet.kamikaze_quota"] {
        assert_eq!(i(row, 2), stage_catalog::get_kamikaze_quota(i(row, 1)));
    }
    assert_eq!(i(&g["fleet.wanderer_wait"][0], 1), stage_catalog::WANDERER_MAX_WAIT);
    for row in &g["player.speed"] {
        assert_eq!(
            hex32(&row[2]),
            stage_catalog::player_compute_speed(i(row, 1)).to_bits(),
            "player.speed({})",
            row[1]
        );
    }
    for row in &g["player.fire_interval"] {
        assert_eq!(
            i(row, 2),
            stage_catalog::player_compute_fire_interval(i(row, 1)),
            "fire_interval({})",
            row[1]
        );
    }
}

#[test]
fn score_hash_parity() {
    let rows = load_vectors();
    let g = group(&rows);
    // Payload list is the dump_vectors.cc contract (index-addressed).
    let payloads = [
        "4;56;26330;3840;2160;60;1792867111;Claudio",
        "",
        "hello",
        "STAGE;WAVE;SCORE",
    ];
    assert_eq!(g["score_hash"].len(), payloads.len());
    for row in &g["score_hash"] {
        let idx = i(row, 1) as usize;
        assert_eq!(row[2], score_hash::line_digest(payloads[idx]), "digest payload {idx}");
    }
    // Self-consistency of the verifier against the corpus digests.
    assert!(score_hash::verify_line(payloads[0], &g["score_hash"][0][2]));
}

#[test]
fn score_parity() {
    let rows = load_vectors();
    let g = group(&rows);

    // Constants contract: the Rust redeclarations must equal the C++
    // GameRules values echoed by the dumper.
    let c = &g["score.consts"];
    assert_eq!(c.len(), 1);
    assert_eq!(i(&c[0], 1), score::INITIAL_SHIELDS);
    assert_eq!(i(&c[0], 2), score::MAX_SHIELD);
    assert_eq!(u(&c[0], 3), score::EXTRA_SHIELD_STEP);
    assert_eq!(i(&c[0], 4), stage_catalog::WAVES_PER_STAGE);
    assert_eq!(i(&c[0], 5), score::MAX_BONUSES_PER_STAGE);

    // Bonus vocabulary (actors.h): speed=1 fire=2 shield=3 multi=4 nuke=5.
    let e = &g["score.bonus_enum"];
    assert_eq!(e.len(), 1);
    for (k, want) in [1u64, 2, 3, 4, 5].iter().enumerate() {
        assert_eq!(u(&e[0], 1 + k), *want, "bonus enum slot {k}");
    }

    // Row-driven replay: each row names its op, so the script lives ONLY in
    // the C++ dumper; the twin executes the corpus and compares every column.
    let ops = &g["score.op"];
    let mut s = score::Score::new();
    for row in ops {
        let idx = i(row, 1);
        let arg = u(row, 3);
        let result = match row[2].as_str() {
            "add" => {
                s.add(arg);
                0
            }
            "milestone" => s.check_extra_shield_milestones(),
            // The arg column is u64; negative shield requests wrap through it
            // (the dumper casts via long long) and unwrap here the same way.
            "shield" => {
                s.set_shield(arg as i64 as i32);
                0
            }
            "inc" => {
                s.inc_level();
                0
            }
            "cheat" => {
                s.set_cheated(arg != 0);
                0
            }
            "can" => u64::from(s.can_spawn_bonus_type(arg as i32)),
            "record" => {
                s.record_bonus_spawned(arg as i32);
                0
            }
            "count" => s.stage_bonuses_spawned_count() as u64,
            "reinit" => {
                s.reinit();
                0
            }
            other => panic!("score.op {idx}: unknown op {other}"),
        };
        assert_eq!(u(row, 4), s.value(), "op {idx} value");
        assert_eq!(
            hex64(&row[5]),
            s.shield_progress().to_bits(),
            "op {idx} progress bits"
        );
        assert_eq!(i(row, 6), s.shield(), "op {idx} shield");
        assert_eq!(i(row, 7), s.level(), "op {idx} level");
        assert_eq!(i(row, 8), s.cycle(), "op {idx} cycle");
        assert_eq!(i(row, 9), i32::from(s.cheated()), "op {idx} cheated");
        assert_eq!(
            u(row, 10),
            u64::from(s.stage_bonus_mask()),
            "op {idx} bonus mask"
        );
        assert_eq!(i(row, 11), s.stage_bonus_cycle(), "op {idx} cycle tag");
        assert_eq!(u(row, 12), result, "op {idx} result");
    }

    let d = &g["score.done"];
    assert_eq!(d.len(), 1);
    assert_eq!(i(&d[0], 1), ops.len() as i32, "score op count");
    assert_eq!(u(&d[0], 2), s.value(), "final value");
    assert_eq!(i(&d[0], 3), s.level(), "final level");
}

#[test]
fn serialize_parity() {
    let rows = load_vectors();
    let g = group(&rows);
    let mut table = highscore::HighScoreTable::new();
    let mk = |value: u64, name: &str, date: i64, w: i32, h: i32, stage: i32, wave: i32| {
        highscore::HighScore {
            value,
            name: name.to_string(),
            date,
            width: w,
            height: h,
            refresh: 60,
            stage,
            wave,
        }
    };
    assert!(table.add(mk(26330, "Claudio", 1792867111, 3840, 2160, 4, 56)));
    assert!(table.add(mk(2520, "Claudio", 1792933633, 3840, 2160, 1, 6)));
    assert!(table.add(mk(670, "Ripley", 1792872474, 1280, 720, 1, 2)));
    // Exact-duplicate rejection (C++ contract).
    assert!(!table.add(mk(2520, "Claudio", 1792933633, 3840, 2160, 1, 6)));

    let corpus: Vec<String> = g["serialize"].iter().map(|r| r[1].clone()).collect();
    assert_eq!(table.serialize_lines(), corpus, "ledger serialization diverged");
}

#[test]
fn spline_parity() {
    let rows = load_vectors();
    let g = group(&rows);
    assert!(g.contains_key("spline.pt"), "corpus sem secao spline -- regenere golden/vectors-cpp.txt");

    // Same literal control sets dumped by tools/dump_vectors.cc.
    let cases: [&[[f32; 2]]; 3] = [
        &[[100.0, -50.0], [260.0, 120.0], [410.0, 90.0], [620.0, 300.0], [700.0, 900.0]],
        &[[0.0, 0.0], [100.0, 100.0]],
        &[[512.0, -40.0], [540.0, 300.0], [512.0, 640.0], [512.0, 780.0]],
    ];
    for (s, ctrls) in cases.iter().enumerate() {
        let controls: Vec<Vec2f> = ctrls.iter().map(|p| Vec2f::new(p[0], p[1])).collect();
        let out = path::generate_smooth_spline(&controls);
        let meta = g["spline.meta"].iter().find(|r| i(r, 1) == s as i32).unwrap();
        assert_eq!(i(meta, 2) as usize, out.len(), "spline {s} point count");
        let pts: Vec<&Vec<String>> = g["spline.pt"].iter().filter(|r| i(r, 1) == s as i32).collect();
        assert_eq!(pts.len(), out.len(), "spline {s} corpus rows");
        for row in pts {
            let j = i(row, 2) as usize;
            assert_eq!(hex32(&row[3]), out[j].x.to_bits(), "spline {s} pt {j} x bits");
            assert_eq!(hex32(&row[4]), out[j].y.to_bits(), "spline {s} pt {j} y bits");
        }
    }
}

#[test]
fn trajectory_parity() {
    let rows = load_vectors();
    let g = group(&rows);
    assert!(g.contains_key("traj.frame"), "corpus sem secao traj -- regenere golden/vectors-cpp.txt");

    // Arrival path straight from the corpus (bit-exact replay inputs).
    let count = i(&g["traj.path_count"][0], 1) as usize;
    let path_rows = &g["traj.path"];
    assert_eq!(path_rows.len(), count);
    let mut arrival = Vec::with_capacity(count);
    for row in path_rows {
        arrival.push(Vec2f::new(
            f32::from_bits(hex32(&row[2])),
            f32::from_bits(hex32(&row[3])),
        ));
    }

    struct Case {
        id: i32,
        mirrored: bool,
        col: i32,
        row: i32,
        seed: u32,
        cycle: i32,
        mode: i32,
    }
    let cases = [
        Case { id: 0, mirrored: false, col: 3, row: 2, seed: 777, cycle: 1, mode: 0 },
        Case { id: 1, mirrored: true, col: 5, row: 3, seed: 778, cycle: 2, mode: 0 },
        Case { id: 2, mirrored: false, col: 4, row: 1, seed: 999, cycle: 1, mode: 1 },
        Case { id: 3, mirrored: false, col: 2, row: 4, seed: 4242, cycle: 3, mode: 2 },
    ];
    let metrics = ViewportMetrics { win_w: 1280, win_h: 720, scale: 1.0 };
    // Docking-arc frames go through atan2/cos/sin: ulps differ across libm
    // versions, so positions carry a tolerance while stages stay exact.
    const POS_TOL: f32 = 1e-2;

    for c in &cases {
        let mut t = Trajectory::new(
            &arrival, c.mirrored, (300, 82), metrics, c.col, c.row, c.seed, c.cycle, 8,
        );
        let init = t.init_position();
        let init_row = g["traj.init"].iter().find(|r| i(r, 1) == c.id).unwrap();
        assert_eq!(hex32(&init_row[2]), (init.0 as f32).to_bits(), "case {} init x", c.id);
        assert_eq!(hex32(&init_row[3]), (init.1 as f32).to_bits(), "case {} init y", c.id);

        if c.mode == 1 {
            t.build_attack((640, 200));
        } else if c.mode == 2 {
            t.build_kamikaze_dive((300, 150), (640, 640));
        }
        if c.mode != 0 {
            let ap = g["traj.attack_pts"].iter().find(|r| i(r, 1) == c.id).unwrap();
            assert_eq!(i(ap, 2) as usize, t.attack_point_count(), "case {} attack points", c.id);
        }

        let frames: Vec<&Vec<String>> = g["traj.frame"].iter().filter(|r| i(r, 1) == c.id).collect();
        assert!(!frames.is_empty(), "case {} has no corpus frames", c.id);
        let mut x = init.0 as f32;
        let mut y = init.1 as f32;
        for (f, row) in frames.iter().enumerate() {
            assert_eq!(i(row, 2), f as i32, "case {} frame index", c.id);
            let (nx, ny) = t.next_position_f(x, y, 6.0);
            x = nx;
            y = ny;
            assert_eq!(t.stage() as i32, i(row, 3), "case {} frame {f} stage", c.id);
            let want_x = f32::from_bits(hex32(&row[4]));
            let want_y = f32::from_bits(hex32(&row[5]));
            assert!(
                (x - want_x).abs() <= POS_TOL && (y - want_y).abs() <= POS_TOL,
                "case {} frame {f} position: rust=({x},{y}) cpp=({want_x},{want_y})",
                c.id
            );
        }
    }
}
#[test]
fn level_parity() {
    let rows = load_vectors();
    let g = group(&rows);
    assert!(
        g.contains_key("level.done"),
        "corpus sem secao level -- regenere golden/vectors-cpp.txt"
    );

    // level.total -- LevelData::GetTotalLevels.
    assert_eq!(i(&g["level.total"][0], 1) as usize, level_data::get_total_levels());

    // Arrival splines. C++ identifies paths by POINTER; the corpus pins them
    // by CONTENT, so the Rust registry (source-declaration order) is matched
    // against the dumped ids (first-encounter order) point-by-point.
    let mut pts_by_id: BTreeMap<i32, Vec<(u32, u32)>> = BTreeMap::new();
    for row in &g["level.path"] {
        let id = i(row, 1);
        let n = i(row, 2) as usize;
        let pts: Vec<&Vec<String>> = g["level.pt"].iter().filter(|r| i(r, 1) == id).collect();
        assert_eq!(pts.len(), n, "level.path {id} point count");
        let mut bits = vec![(0u32, 0u32); n];
        for p in pts {
            let j = i(p, 2) as usize;
            assert!(j < n, "level.pt {id} index {j} out of range");
            bits[j] = (hex32(&p[3]), hex32(&p[4]));
        }
        assert!(pts_by_id.insert(id, bits).is_none(), "duplicate level.path {id}");
    }

    let rust_paths = level_data::paths();
    let rust_bits: Vec<Vec<(u32, u32)>> = rust_paths
        .iter()
        .map(|p| p.iter().map(|q| (q.x.to_bits(), q.y.to_bits())).collect())
        .collect();
    assert_eq!(pts_by_id.len(), rust_paths.len(), "registry size");
    for (rid, bits) in rust_bits.iter().enumerate() {
        let hits = pts_by_id.values().filter(|v| *v == bits).count();
        assert_eq!(hits, 1, "rust path {rid} must match exactly one dumped path by content");
    }

    // Convoy tables: per-wave lengths and every field of every entry.
    for row in &g["level.table"] {
        let (stage, idx, n) = (i(row, 1), i(row, 2), i(row, 3) as usize);
        let level = if stage == 1 { idx + 1 } else { 16 + idx };
        let table = level_data::get_convoy_data(level as usize);
        assert_eq!(table.len(), n, "table stage {stage} wave {idx} length");
    }
    for row in &g["level.convoy"] {
        let (stage, idx, k) = (i(row, 1), i(row, 2), i(row, 3) as usize);
        let level = if stage == 1 { idx + 1 } else { 16 + idx };
        let spec = &level_data::get_convoy_data(level as usize)[k];
        let ctx = format!("convoy stage {stage} wave {idx} entry {k}");
        assert_eq!(i(row, 4), spec.wait, "{ctx} wait");
        assert_eq!(i(row, 5), spec.texture_id as i32, "{ctx} texture_id");
        assert_eq!(i(row, 6), spec.convoy_size_pc, "{ctx} convoy_size_pc");
        assert_eq!(i(row, 7), i32::from(spec.x_mirror), "{ctx} x_mirror");
        assert_eq!(i(row, 8), i32::from(spec.split), "{ctx} split");
        assert_eq!(i(row, 9), i32::from(spec.simultaneous), "{ctx} simultaneous");
        assert_eq!(i(row, 10), spec.dock_align, "{ctx} dock_align");
        let pid = i(row, 11);
        assert_eq!(
            pts_by_id.get(&pid).unwrap(),
            &rust_bits[spec.path_id],
            "{ctx} arrival path content"
        );
    }

    // Resolution mapping: production GetConvoyData across the first cycle,
    // both stage/wrap boundaries and deep repeating levels.
    let total = level_data::get_total_levels() as i32;
    for row in &g["level.resolve"] {
        let level = i(row, 1);
        let effective = if level == 0 { 1 } else { level };
        let stage = stage_catalog::wave_to_stage(effective);
        let tbl = if stage >= 2 { 2 } else { 1 };
        assert_eq!(i(row, 2), stage, "resolve {level} stage");
        assert_eq!(i(row, 3), (effective - 1) % total, "resolve {level} idx");
        assert_eq!(i(row, 4), tbl, "resolve {level} table");
        let table = level_data::get_convoy_data(level.max(0) as usize);
        assert_eq!(i(row, 5) as usize, table.len(), "resolve {level} n");
        assert_eq!(i(row, 6), table[0].wait, "resolve {level} first wait");
        assert_eq!(i(row, 7), table[0].texture_id as i32, "resolve {level} first texture");
    }

    // Aggregates: registry size, total resampled points, live convoys, tables.
    let done = &g["level.done"][0];
    assert_eq!(i(done, 1) as usize, rust_paths.len(), "done n_paths");
    let total_pts: usize = rust_bits.iter().map(|b| b.len()).sum();
    assert_eq!(i(done, 2) as usize, total_pts, "done total_pts");
    let convoys: usize = g["level.table"].iter().map(|r| i(r, 3) as usize).sum();
    assert_eq!(i(done, 3) as usize, convoys, "done n_convoys");
    assert_eq!(i(done, 4), g["level.table"].len() as i32, "done n_tables");
    assert_eq!(g["level.convoy"].len(), convoys, "convoy row count");
}
