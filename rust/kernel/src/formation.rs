//! Honeycomb super-table: port of src/entities/formation_grid.{h,cc}.
//! The C++ computes it at compile time; stable Rust const fn lacks float
//! arithmetic, so the table is built once at startup (OnceLock). Bit-exact
//! parity with the .rodata original is enforced by the golden vectors.

use crate::mathx::fast_round_f32;
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
