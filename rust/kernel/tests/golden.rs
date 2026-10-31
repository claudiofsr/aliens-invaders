//! Golden-vector parity (RUST.md Phase 1/2a): every covered line of
//! golden/vectors-cpp.txt must match the Rust port BIT FOR BIT. Sections of
//! later phases (profile, fleet.*, player.*, score_hash, serialize) are
//! counted to prove the corpus parses, but not asserted yet.

use kernel::{formation, mathx, pacing, random_stream::RandomStream};
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
fn later_phase_sections_present() {
    // Proves the whole corpus parses and the future work list is real.
    let rows = load_vectors();
    let g = group(&rows);
    for key in ["profile", "fleet.speed", "player.speed", "score_hash", "serialize"] {
        assert!(!g.get(key).map_or(true, Vec::is_empty), "section {key} missing");
    }
}
