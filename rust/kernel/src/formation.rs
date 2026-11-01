//! Honeycomb super-table: port of src/entities/formation_grid.{h,cc}.
//! The C++ computes it at compile time; stable Rust const fn lacks float
//! arithmetic, so the table is built once at startup (OnceLock). Bit-exact
//! parity with the .rodata original is enforced by the golden vectors.

use crate::mathx::{chebyshev, fast_round_f32, floor_div};
use crate::random_stream::RandomStream;
use std::sync::OnceLock;

pub const MAX_GRID_ROWS: usize = 12;
pub const MAX_GRID_COLS: usize = 16;
pub const NUM_STAGE_MODES: usize = 5;

// Tuning constants mirrored from src/core/constants.h (GameRules::Fleet).
// The golden vectors are the watchdog of this duplication (RUST.md rule 4).
const ROW_STAGGER: f32 = 0.50;
const STAGE2_CURVE_CENTER: i32 = 4;
const STAGE2_CURVE_PLATEAU: i32 = 3;
const STAGE3_WAVE_DIP: f32 = 0.25;
const STAGE3_FLANK_DIVISOR: f32 = 6.0;
const STAGE4_WEDGE_HEIGHT: f32 = 1.25;
const STAGE4_WEDGE_DIVISOR: f32 = 5.0;
const STAGE5_WAVE_DIP: f32 = 0.50;
const STAGE5_FLANK_DIVISOR: f32 = 4.0;
const ALIEN_BASE_WIDTH_PX: f32 = 84.0;
const ALIEN_BASE_HEIGHT_PX: f32 = 84.0;
const H_SPACING_PX: f32 = 93.0;
const V_SPACING_PX: f32 = 95.0;
const BASE_CRUISE_Y_PX: f32 = 82.0;

#[derive(Clone, Copy)]
pub struct UnitCoord {
    pub offset_x: f32,
    pub offset_y: f32,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct FormationSlot {
    pub offset_x: i32,
    pub offset_y: i32,
}

pub type UnitGrid = [[[UnitCoord; MAX_GRID_COLS]; MAX_GRID_ROWS]; NUM_STAGE_MODES];

fn build_unit_grid() -> UnitGrid {
    let mut tbl = [[[UnitCoord {
        offset_x: 0.0,
        offset_y: 0.0,
    }; MAX_GRID_COLS]; MAX_GRID_ROWS]; NUM_STAGE_MODES];
    for mode in 0..NUM_STAGE_MODES {
        for row in 0..MAX_GRID_ROWS {
            let stagger = if row % 2 != 0 { ROW_STAGGER } else { 0.0 };
            for col in 0..MAX_GRID_COLS {
                let mut offset_y_unit = 0.0f32;
                let rel_col = col as i32 - STAGE2_CURVE_CENTER;
                // Core-language absolute value: same spelling the C++ uses.
                let abs_col = if rel_col < 0 { -rel_col } else { rel_col };
                if mode == 1 {
                    let val = (STAGE2_CURVE_CENTER - abs_col).max(0);
                    offset_y_unit = if val > STAGE2_CURVE_PLATEAU {
                        1.0
                    } else {
                        val as f32 / STAGE2_CURVE_PLATEAU as f32
                    };
                } else if mode == 2 {
                    let wave_dip = if abs_col % 2 == 1 { STAGE3_WAVE_DIP } else { 0.0 };
                    let flank_rise = (abs_col as f32 / STAGE3_FLANK_DIVISOR).min(1.0);
                    offset_y_unit = wave_dip + flank_rise;
                } else if mode == 3 {
                    let wedge = (1.0 - abs_col as f32 / STAGE4_WEDGE_DIVISOR).max(0.0);
                    offset_y_unit = wedge * STAGE4_WEDGE_HEIGHT;
                } else if mode == 4 {
                    let wave_dip = if abs_col % 2 == 1 { STAGE5_WAVE_DIP } else { 0.0 };
                    let flank_rise = (abs_col as f32 / STAGE5_FLANK_DIVISOR).min(1.0);
                    offset_y_unit = wave_dip + flank_rise;
                }
                tbl[mode][row][col].offset_x = col as f32 + stagger;
                tbl[mode][row][col].offset_y = row as f32 + offset_y_unit;
            }
        }
    }
    tbl
}

/// The normalized unit offsets, built once per process (the C++ twin is .rodata).
pub fn unit_grid() -> &'static UnitGrid {
    static GRID: OnceLock<UnitGrid> = OnceLock::new();
    GRID.get_or_init(build_unit_grid)
}

/// Resolution-scaled fleet metrics + the full slot table for one scale.
#[derive(Clone)]
pub struct GridData {
    pub width: i32,
    pub height: i32,
    pub spacing_x: i32,
    pub spacing_y: i32,
    pub base_cruise_y: i32,
    pub slots: [[[FormationSlot; MAX_GRID_COLS]; MAX_GRID_ROWS]; NUM_STAGE_MODES],
}

/// Port of FormationGrid::Recompute(scale): identical formula order so the
/// rounded integers come out bit-identical to the C++.
pub fn recompute(scale: f32) -> GridData {
    let mut data = GridData {
        width: fast_round_f32(ALIEN_BASE_WIDTH_PX * scale),
        height: fast_round_f32(ALIEN_BASE_HEIGHT_PX * scale),
        spacing_x: fast_round_f32(H_SPACING_PX * scale),
        spacing_y: fast_round_f32(V_SPACING_PX * scale),
        base_cruise_y: fast_round_f32(BASE_CRUISE_Y_PX * scale),
        slots: [[[FormationSlot {
            offset_x: 0,
            offset_y: 0,
        }; MAX_GRID_COLS]; MAX_GRID_ROWS]; NUM_STAGE_MODES],
    };
    let spacing_x_f = H_SPACING_PX * scale;
    let spacing_y_f = V_SPACING_PX * scale;
    let grid = unit_grid();
    for mode in 0..NUM_STAGE_MODES {
        for row in 0..MAX_GRID_ROWS {
            for col in 0..MAX_GRID_COLS {
                let u = &grid[mode][row][col];
                data.slots[mode][row][col] = FormationSlot {
                    offset_x: fast_round_f32(u.offset_x * spacing_x_f),
                    offset_y: fast_round_f32(u.offset_y * spacing_y_f),
                };
            }
        }
    }
    data
}

/// Port of FormationGrid::GetSlot: stage_cycle is 1-based, all axes clamp.
pub fn get_slot(data: &GridData, stage_cycle: i32, row: i32, col: i32) -> FormationSlot {
    let mode = (stage_cycle - 1).clamp(0, NUM_STAGE_MODES as i32 - 1) as usize;
    let r = row.clamp(0, MAX_GRID_ROWS as i32 - 1) as usize;
    let c = col.clamp(0, MAX_GRID_COLS as i32 - 1) as usize;
    data.slots[mode][r][c]
}


// ---- Relativistic warp (Alien14, spec Priorities 1-4) ----------------------
// Port of FormationGrid::ComputeRelativisticWarp / PickRestrictedWarpCell
// (inline in src/entities/formation_grid.h). The constants mirror
// src/core/constants.h; the golden vectors police any drift on both sides.
const WARP_MIN_CELL_SEPARATION: i32 = 5;
const PLAYER_Y_OFFSET_PIXELS: i32 = 86;
const FLEET_MARGIN_X_PIXELS: i32 = 14;
const FLEET_MARGIN_Y_MIN_PIXELS: i32 = 16;
const FLEET_MARGIN_Y_MAX_OFFSET_PIXELS: i32 = 180;

/// Twin of FormationGrid::WarpDecision: cell station plus clamped pixel target.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct WarpDecision {
    pub col: i32,
    pub row: i32,
    pub target_x: i32,
    pub target_y: i32,
}

/// O(1) restricted draw of a honeycomb cell outside BOTH Chebyshev exclusion
/// squares (jumper zone and player zone). The rectangle partition, the single
/// UniformInt draw and the row-major index walk mirror the C++ exactly, so the
/// RNG consumption -- and every later draw -- stays in lockstep.
fn pick_restricted_warp_cell(
    origin_col: i32,
    origin_row: i32,
    player_col: i32,
    player_row: i32,
    used_cols: i32,
    used_rows: i32,
    n_in: i32,
    rng: &mut RandomStream,
) -> (i32, i32) {
    if used_cols <= 0 || used_rows <= 0 {
        return (0, 0);
    }
    let n = n_in.max(1);

    // Exclusion square [c0, r0, c1, r1] (inclusive) clamped to the domain;
    // inverted bounds mean "covers nothing".
    let make_square = |cc: i32, cr: i32| -> [i32; 4] {
        [
            (cc - n).max(0),
            (cr - n).max(0),
            (cc + n).min(used_cols - 1),
            (cr + n).min(used_rows - 1),
        ]
    };
    let z1 = make_square(origin_col, origin_row);
    let z2 = make_square(player_col, player_row);
    let zones = [z1, z2];

    // Row boundaries: domain edges plus both squares' row spans slice the
    // domain into at most 5 bands; within one band each zone covers every row
    // or none. The C++ spells an insertion sort; sort_unstable yields the
    // same ascending order for integers.
    let mut bounds = [0, z1[1], z1[3] + 1, z2[1], z2[3] + 1, used_rows];
    for b in bounds.iter_mut() {
        *b = (*b).clamp(0, used_rows);
    }
    bounds.sort_unstable();

    const MAX_RECTS: usize = 15;
    let mut rects = [(0i32, 0i32, 0i32, 0i32); MAX_RECTS]; // (c0, r0, cols, rows)
    let mut rect_count = 0usize;
    let mut total = 0i32;
    let mut bi = 0usize;
    while bi + 1 < 6 && rect_count < MAX_RECTS {
        let rb0 = bounds[bi];
        let rb1 = bounds[bi + 1];
        bi += 1;
        if rb1 <= rb0 {
            continue;
        }

        // Column intervals excluded inside this band (at most one per zone).
        let mut ic0 = [0i32; 2];
        let mut ic1 = [0i32; 2];
        let mut ic_n = 0usize;
        for s in zones.iter() {
            if s[0] > s[2] || s[1] > s[3] {
                continue; // covers nothing
            }
            if s[1] <= rb0 && rb1 <= s[3] + 1 {
                ic0[ic_n] = s[0];
                ic1[ic_n] = s[2];
                ic_n += 1;
            }
        }
        if ic_n == 2 && ic0[1] < ic0[0] {
            ic0.swap(0, 1);
            ic1.swap(0, 1);
        }

        // Cursor sweep: the gaps between the (implicitly merged) intervals
        // are the valid column runs of this band.
        let band_rows = rb1 - rb0;
        let mut cursor = 0i32;
        let mut kz = 0usize;
        while kz < ic_n && rect_count < MAX_RECTS {
            if ic0[kz] > cursor {
                rects[rect_count] = (cursor, rb0, ic0[kz] - cursor, band_rows);
                rect_count += 1;
                total += (ic0[kz] - cursor) * band_rows;
            }
            cursor = cursor.max(ic1[kz] + 1);
            kz += 1;
        }
        if cursor < used_cols && rect_count < MAX_RECTS {
            rects[rect_count] = (cursor, rb0, used_cols - cursor, band_rows);
            rect_count += 1;
            total += (used_cols - cursor) * band_rows;
        }
    }

    if total <= 0 {
        return (0, 0); // degenerate: zones cover all
    }

    // Single draw, single bounded walk, exact index -> cell mapping
    // (row-major inside each rectangle, rectangles in band order).
    let mut idx = rng.uniform_int(0, total - 1);
    for ri in 0..rect_count {
        let (c0, r0, cols, rows) = rects[ri];
        let area = cols * rows;
        if idx < area {
            let lr = idx / cols;
            let lc = idx - lr * cols;
            return (c0 + lc, r0 + lr);
        }
        idx -= area;
    }
    (0, 0) // unreachable: idx < total == sum(areas)
}

/// Twin of FormationGrid::ComputeRelativisticWarp: restricted draw, occupancy
/// repair walk, far-corner fallback and the single-step margin clamp.
#[allow(clippy::too_many_arguments)]
pub fn compute_relativistic_warp(
    data: &GridData,
    origin_col: i32,
    origin_row: i32,
    used_cols: i32,
    used_rows: i32,
    player_x: i32,
    player_y: i32,
    base_cruise: (i32, i32),
    stage_cycle: i32,
    scale: f32,
    window_w: i32,
    window_h: i32,
    occupancy: &[u16; MAX_GRID_ROWS],
    rng: &mut RandomStream,
) -> WarpDecision {
    let n = WARP_MIN_CELL_SEPARATION;
    let sp_x = data.spacing_x.max(1);
    let sp_y = data.spacing_y.max(1);
    let p_y = if player_y > 0 {
        player_y
    } else {
        window_h - PLAYER_Y_OFFSET_PIXELS
    };
    // Floored division: a player left of the formation anchor maps to a
    // negative cell index; truncation would shift the player zone by one cell
    // and let landings sneak closer to the formation.
    let player_col = floor_div(player_x - base_cruise.0, sp_x);
    let player_row = floor_div(p_y - base_cruise.1, sp_y);

    // Priority 1: single draw over the active domain minus both zones.
    let picked = pick_restricted_warp_cell(
        origin_col, origin_row, player_col, player_row, used_cols, used_rows, n, rng,
    );

    let is_occupied = |c: i32, r: i32| -> bool {
        if r < 0 || r >= MAX_GRID_ROWS as i32 || c < 0 || c >= used_cols {
            return true;
        }
        (occupancy[r as usize] & (1u16 << c)) != 0
    };
    let outside_exclusion = |c: i32, r: i32| -> bool {
        chebyshev(c, r, origin_col, origin_row) > n
            && chebyshev(c, r, player_col, player_row) > n
    };

    let mut dest_col = picked.0;
    let mut dest_row = picked.1;

    // Priority 2: occupancy repair. The draw already guarantees both
    // exclusion zones; the re-check only defends the degenerate fallback.
    if is_occupied(dest_col, dest_row) || !outside_exclusion(dest_col, dest_row) {
        let slot = get_slot(data, stage_cycle, dest_row, dest_col);
        let dest_x = base_cruise.0 + slot.offset_x;
        let dir = if dest_x >= player_x { 1 } else { -1 };

        let mut found = false;
        let mut step = 1;
        while step < used_cols {
            let c = dest_col + step * dir;
            if c < 0 || c >= used_cols {
                break;
            }
            if outside_exclusion(c, dest_row) && !is_occupied(c, dest_row) {
                dest_col = c;
                found = true;
                break;
            }
            step += 1;
        }

        // Priority 3: no valid cell -- jump to the top corner farthest from
        // the player ship. The corner is a PREFERENCE, never a right.
        if !found {
            dest_row = 0;
            dest_col = if player_x >= window_w / 2 {
                0
            } else {
                used_cols - 1
            };
            if is_occupied(dest_col, dest_row) {
                let c_dir = if dest_col == 0 { 1 } else { -1 };
                let mut placed = false;
                let mut r = 0;
                while r < used_rows && !placed {
                    let mut step = 0;
                    while step < used_cols {
                        let c = dest_col + step * c_dir;
                        if c < 0 || c >= used_cols {
                            break;
                        }
                        if !is_occupied(c, r) {
                            dest_row = r;
                            dest_col = c;
                            placed = true;
                            break;
                        }
                        step += 1;
                    }
                    r += 1;
                }
            }
        }
    }

    // Priority 4a: single-step clamp within the screen margins.
    let final_slot = get_slot(data, stage_cycle, dest_row, dest_col);
    let half_w = data.width / 2;
    let half_h = data.height / 2;
    let margin_x = half_w + fast_round_f32(FLEET_MARGIN_X_PIXELS as f32 * scale);
    let min_y = half_h + fast_round_f32(FLEET_MARGIN_Y_MIN_PIXELS as f32 * scale);
    let max_y = window_h - half_h - fast_round_f32(FLEET_MARGIN_Y_MAX_OFFSET_PIXELS as f32 * scale);
    let max_x = window_w - margin_x;

    let target_x = (base_cruise.0 + final_slot.offset_x).clamp(margin_x, margin_x.max(max_x));
    let target_y = (base_cruise.1 + final_slot.offset_y).clamp(min_y, min_y.max(max_y));

    WarpDecision { col: dest_col, row: dest_row, target_x, target_y }
}
