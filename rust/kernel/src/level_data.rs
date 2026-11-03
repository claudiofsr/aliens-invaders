//! Port of src/entities/level_data.cc: the hand-authored convoy catalog.
//!
//! GENERATED FILE -- do not edit. Regenerate with the extraction script
//! after any change to the C++ source (tools/extract_level.py).
//!
//! Two halves, mirroring the C++ contract exactly:
//! - `paths()`: the arrival splines, resampled through the SAME golden-
//!   pinned Catmull-Rom + arc-length resampler as production (path.rs).
//!   The corpus `level.pt` rows pin every resampled point bit-exactly.
//! - `get_convoy_data(level)`: the per-wave convoy tables (15 stage-1 +
//!   15 stage-2), resolved with the production modulo/stage mapping.
//!   The corpus `level.table` / `level.convoy` / `level.resolve` rows pin
//!   every field. The C++ sentinel row (TextureId::None) is excluded --
//!   Rust tables carry their length in the slice.
//!
//! The two `FlightPath::Loop` factories in the C++ file are referenced by
//! no convoy table and are therefore not part of the catalog contract.

use std::sync::OnceLock;

use crate::fleet::ConvoySpec;
use crate::path::{generate_smooth_spline, Vec2f};
use crate::stage_catalog::{wave_to_stage, WAVES_PER_STAGE};
use crate::textures::TextureId;

// ------------------------------------------------ control-point splines

const HANG_LEFT_CTRL: &[Vec2f] = &[
    Vec2f { x: 280.0, y: -30.0 }, Vec2f { x: 280.0, y: 400.0 }, Vec2f { x: 270.0, y: 520.0 },
    Vec2f { x: 240.0, y: 615.0 }, Vec2f { x: 180.0, y: 670.0 }, Vec2f { x: 100.0, y: 700.0 },
    Vec2f { x: 15.0, y: 675.0 }, Vec2f { x: -35.0, y: 610.0 }, Vec2f { x: -50.0, y: 520.0 },
    Vec2f { x: -10.0, y: 420.0 }, Vec2f { x: 90.0, y: 330.0 }, Vec2f { x: 190.0, y: 280.0 },
    Vec2f { x: 280.0, y: 250.0 },
];

const HANG_RIGHT_CTRL: &[Vec2f] = &[
    Vec2f { x: 744.0, y: -30.0 }, Vec2f { x: 744.0, y: 400.0 }, Vec2f { x: 754.0, y: 520.0 },
    Vec2f { x: 784.0, y: 615.0 }, Vec2f { x: 844.0, y: 670.0 }, Vec2f { x: 924.0, y: 700.0 },
    Vec2f { x: 1009.0, y: 675.0 }, Vec2f { x: 1059.0, y: 610.0 }, Vec2f { x: 1074.0, y: 520.0 },
    Vec2f { x: 1034.0, y: 420.0 }, Vec2f { x: 934.0, y: 330.0 }, Vec2f { x: 834.0, y: 280.0 },
    Vec2f { x: 744.0, y: 250.0 },
];

const VALKYRIE_CTRL: &[Vec2f] = &[
    Vec2f { x: -30.0, y: 180.0 }, Vec2f { x: 160.0, y: 320.0 }, Vec2f { x: 340.0, y: 480.0 },
    Vec2f { x: 512.0, y: 540.0 }, Vec2f { x: 680.0, y: 480.0 }, Vec2f { x: 860.0, y: 320.0 },
    Vec2f { x: 1054.0, y: 180.0 },
];

const ACROBATIC_LEFT_CTRL: &[Vec2f] = &[
    Vec2f { x: -30.0, y: 80.0 }, Vec2f { x: 170.0, y: 240.0 }, Vec2f { x: 290.0, y: 440.0 },
    Vec2f { x: 250.0, y: 660.0 }, Vec2f { x: 130.0, y: 730.0 }, Vec2f { x: -30.0, y: 680.0 },
    Vec2f { x: -60.0, y: 520.0 }, Vec2f { x: 20.0, y: 370.0 }, Vec2f { x: 160.0, y: 280.0 },
    Vec2f { x: 290.0, y: 240.0 },
];

const ACROBATIC_RIGHT_CTRL: &[Vec2f] = &[
    Vec2f { x: 1054.0, y: 80.0 }, Vec2f { x: 854.0, y: 240.0 }, Vec2f { x: 734.0, y: 440.0 },
    Vec2f { x: 774.0, y: 660.0 }, Vec2f { x: 894.0, y: 730.0 }, Vec2f { x: 1054.0, y: 680.0 },
    Vec2f { x: 1084.0, y: 520.0 }, Vec2f { x: 1004.0, y: 370.0 }, Vec2f { x: 864.0, y: 280.0 },
    Vec2f { x: 734.0, y: 240.0 },
];

const HYPERBOLA_CTRL: &[Vec2f] = &[
    Vec2f { x: -30.0, y: 80.0 }, Vec2f { x: 220.0, y: 240.0 }, Vec2f { x: 440.0, y: 420.0 },
    Vec2f { x: 600.0, y: 520.0 }, Vec2f { x: 760.0, y: 480.0 }, Vec2f { x: 890.0, y: 360.0 },
    Vec2f { x: 1054.0, y: 220.0 },
];

const BUTTERFLY_LEFT_CTRL: &[Vec2f] = &[
    Vec2f { x: 240.0, y: -30.0 }, Vec2f { x: 155.0, y: 200.0 }, Vec2f { x: 90.0, y: 410.0 },
    Vec2f { x: 170.0, y: 590.0 }, Vec2f { x: 330.0, y: 530.0 }, Vec2f { x: 400.0, y: 400.0 },
    Vec2f { x: 330.0, y: 290.0 }, Vec2f { x: 240.0, y: 250.0 }, Vec2f { x: 155.0, y: 230.0 },
];

const BUTTERFLY_RIGHT_CTRL: &[Vec2f] = &[
    Vec2f { x: 784.0, y: -30.0 }, Vec2f { x: 869.0, y: 200.0 }, Vec2f { x: 934.0, y: 410.0 },
    Vec2f { x: 854.0, y: 590.0 }, Vec2f { x: 694.0, y: 530.0 }, Vec2f { x: 624.0, y: 400.0 },
    Vec2f { x: 694.0, y: 290.0 }, Vec2f { x: 784.0, y: 250.0 }, Vec2f { x: 869.0, y: 230.0 },
];

const HELIX_LEFT_CTRL: &[Vec2f] = &[
    Vec2f { x: 180.0, y: -30.0 }, Vec2f { x: 130.0, y: 230.0 }, Vec2f { x: 340.0, y: 410.0 },
    Vec2f { x: 190.0, y: 570.0 }, Vec2f { x: 350.0, y: 710.0 }, Vec2f { x: 250.0, y: 830.0 },
    Vec2f { x: 130.0, y: 700.0 }, Vec2f { x: 230.0, y: 390.0 },
];

const HELIX_RIGHT_CTRL: &[Vec2f] = &[
    Vec2f { x: 844.0, y: -30.0 }, Vec2f { x: 894.0, y: 230.0 }, Vec2f { x: 684.0, y: 410.0 },
    Vec2f { x: 834.0, y: 570.0 }, Vec2f { x: 674.0, y: 710.0 }, Vec2f { x: 774.0, y: 830.0 },
    Vec2f { x: 894.0, y: 700.0 }, Vec2f { x: 794.0, y: 390.0 },
];

const HORSESHOE_CTRL: &[Vec2f] = &[
    Vec2f { x: 200.0, y: -30.0 }, Vec2f { x: 220.0, y: 320.0 }, Vec2f { x: 280.0, y: 540.0 },
    Vec2f { x: 380.0, y: 670.0 }, Vec2f { x: 512.0, y: 720.0 }, Vec2f { x: 644.0, y: 670.0 },
    Vec2f { x: 744.0, y: 540.0 }, Vec2f { x: 804.0, y: 320.0 }, Vec2f { x: 824.0, y: -30.0 },
];

const BALLET_X_TOP_LEFT_CTRL: &[Vec2f] = &[
    Vec2f { x: -50.0, y: -50.0 }, Vec2f { x: 140.0, y: 140.0 }, Vec2f { x: 260.0, y: 260.0 },
    Vec2f { x: 220.0, y: 480.0 }, Vec2f { x: 160.0, y: 720.0 }, Vec2f { x: 220.0, y: 880.0 },
    Vec2f { x: 340.0, y: 840.0 }, Vec2f { x: 300.0, y: 620.0 }, Vec2f { x: 180.0, y: 420.0 },
    Vec2f { x: 180.0, y: 220.0 },
];

const BALLET_X_TOP_RIGHT_CTRL: &[Vec2f] = &[
    Vec2f { x: 1074.0, y: -50.0 }, Vec2f { x: 884.0, y: 140.0 }, Vec2f { x: 764.0, y: 260.0 },
    Vec2f { x: 804.0, y: 480.0 }, Vec2f { x: 864.0, y: 720.0 }, Vec2f { x: 804.0, y: 880.0 },
    Vec2f { x: 684.0, y: 840.0 }, Vec2f { x: 724.0, y: 620.0 }, Vec2f { x: 844.0, y: 420.0 },
    Vec2f { x: 844.0, y: 220.0 },
];

const BALLET_X_BOTTOM_LEFT_CTRL: &[Vec2f] = &[
    Vec2f { x: -50.0, y: 920.0 }, Vec2f { x: 140.0, y: 800.0 }, Vec2f { x: 260.0, y: 680.0 },
    Vec2f { x: 320.0, y: 480.0 }, Vec2f { x: 220.0, y: 320.0 }, Vec2f { x: 140.0, y: 400.0 },
    Vec2f { x: 160.0, y: 600.0 }, Vec2f { x: 280.0, y: 820.0 }, Vec2f { x: 240.0, y: 600.0 },
    Vec2f { x: 200.0, y: 240.0 },
];

const BALLET_X_BOTTOM_RIGHT_CTRL: &[Vec2f] = &[
    Vec2f { x: 1074.0, y: 920.0 }, Vec2f { x: 884.0, y: 800.0 }, Vec2f { x: 764.0, y: 680.0 },
    Vec2f { x: 704.0, y: 480.0 }, Vec2f { x: 804.0, y: 320.0 }, Vec2f { x: 884.0, y: 400.0 },
    Vec2f { x: 864.0, y: 600.0 }, Vec2f { x: 744.0, y: 820.0 }, Vec2f { x: 784.0, y: 600.0 },
    Vec2f { x: 824.0, y: 240.0 },
];

const SCYTHE_LEFT_CTRL: &[Vec2f] = &[
    Vec2f { x: -40.0, y: 100.0 }, Vec2f { x: 220.0, y: 280.0 }, Vec2f { x: 490.0, y: 450.0 },
    Vec2f { x: 730.0, y: 590.0 }, Vec2f { x: 900.0, y: 710.0 }, Vec2f { x: 950.0, y: 560.0 },
    Vec2f { x: 850.0, y: 380.0 }, Vec2f { x: 660.0, y: 260.0 },
];

const COBRA_CTRL: &[Vec2f] = &[
    Vec2f { x: -30.0, y: 620.0 }, Vec2f { x: 160.0, y: 580.0 }, Vec2f { x: 330.0, y: 470.0 },
    Vec2f { x: 450.0, y: 370.0 }, Vec2f { x: 512.0, y: 330.0 }, Vec2f { x: 574.0, y: 370.0 },
    Vec2f { x: 694.0, y: 470.0 }, Vec2f { x: 864.0, y: 580.0 }, Vec2f { x: 1054.0, y: 620.0 },
];

const SINE_LEFT_CTRL: &[Vec2f] = &[
    Vec2f { x: -30.0, y: 480.0 }, Vec2f { x: 150.0, y: 430.0 }, Vec2f { x: 310.0, y: 530.0 },
    Vec2f { x: 460.0, y: 640.0 }, Vec2f { x: 370.0, y: 460.0 }, Vec2f { x: 190.0, y: 330.0 },
    Vec2f { x: 270.0, y: 210.0 },
];

const SINE_RIGHT_CTRL: &[Vec2f] = &[
    Vec2f { x: 1054.0, y: 480.0 }, Vec2f { x: 874.0, y: 430.0 }, Vec2f { x: 714.0, y: 530.0 },
    Vec2f { x: 564.0, y: 640.0 }, Vec2f { x: 654.0, y: 460.0 }, Vec2f { x: 834.0, y: 330.0 },
    Vec2f { x: 754.0, y: 210.0 },
];

const CASCADE_LEFT_CTRL: &[Vec2f] = &[
    Vec2f { x: 180.0, y: -30.0 }, Vec2f { x: 220.0, y: 200.0 }, Vec2f { x: 160.0, y: 400.0 },
    Vec2f { x: 240.0, y: 620.0 },
];

const CASCADE_RIGHT_CTRL: &[Vec2f] = &[
    Vec2f { x: 844.0, y: -30.0 }, Vec2f { x: 804.0, y: 200.0 }, Vec2f { x: 864.0, y: 400.0 },
    Vec2f { x: 784.0, y: 620.0 },
];

const CLOVERLEAF_LEFT_CTRL: &[Vec2f] = &[
    Vec2f { x: 260.0, y: -30.0 }, Vec2f { x: 260.0, y: 310.0 }, Vec2f { x: 370.0, y: 390.0 },
    Vec2f { x: 420.0, y: 480.0 }, Vec2f { x: 375.0, y: 570.0 }, Vec2f { x: 270.0, y: 545.0 },
    Vec2f { x: 165.0, y: 570.0 }, Vec2f { x: 120.0, y: 480.0 }, Vec2f { x: 170.0, y: 390.0 },
    Vec2f { x: 260.0, y: 310.0 }, Vec2f { x: 260.0, y: 230.0 },
];

const CLOVERLEAF_RIGHT_CTRL: &[Vec2f] = &[
    Vec2f { x: 764.0, y: -30.0 }, Vec2f { x: 764.0, y: 310.0 }, Vec2f { x: 874.0, y: 390.0 },
    Vec2f { x: 924.0, y: 480.0 }, Vec2f { x: 879.0, y: 570.0 }, Vec2f { x: 774.0, y: 545.0 },
    Vec2f { x: 669.0, y: 570.0 }, Vec2f { x: 624.0, y: 480.0 }, Vec2f { x: 674.0, y: 390.0 },
    Vec2f { x: 764.0, y: 310.0 }, Vec2f { x: 764.0, y: 230.0 },
];

const GEODESIC_SLINGSHOT_LEFT_CTRL: &[Vec2f] = &[
    Vec2f { x: -40.0, y: 80.0 }, Vec2f { x: 150.0, y: 280.0 }, Vec2f { x: 270.0, y: 530.0 },
    Vec2f { x: 230.0, y: 750.0 }, Vec2f { x: 110.0, y: 710.0 }, Vec2f { x: 70.0, y: 490.0 },
    Vec2f { x: 170.0, y: 320.0 }, Vec2f { x: 290.0, y: 260.0 },
];

const GEODESIC_SLINGSHOT_RIGHT_CTRL: &[Vec2f] = &[
    Vec2f { x: 1064.0, y: 80.0 }, Vec2f { x: 874.0, y: 280.0 }, Vec2f { x: 754.0, y: 530.0 },
    Vec2f { x: 794.0, y: 750.0 }, Vec2f { x: 914.0, y: 710.0 }, Vec2f { x: 954.0, y: 490.0 },
    Vec2f { x: 854.0, y: 320.0 }, Vec2f { x: 734.0, y: 260.0 },
];

// ------------------------------------------------------- path registry
pub const PATH_HANG_LEFT: usize = 0;
pub const PATH_HANG_RIGHT: usize = 1;
pub const PATH_VALKYRIE: usize = 2;
pub const PATH_ACROBATIC_LEFT: usize = 3;
pub const PATH_ACROBATIC_RIGHT: usize = 4;
pub const PATH_HYPERBOLA: usize = 5;
pub const PATH_BUTTERFLY_LEFT: usize = 6;
pub const PATH_BUTTERFLY_RIGHT: usize = 7;
pub const PATH_HELIX_LEFT: usize = 8;
pub const PATH_HELIX_RIGHT: usize = 9;
pub const PATH_HORSESHOE: usize = 10;
pub const PATH_BALLET_X_TOP_LEFT: usize = 11;
pub const PATH_BALLET_X_TOP_RIGHT: usize = 12;
pub const PATH_BALLET_X_BOTTOM_LEFT: usize = 13;
pub const PATH_BALLET_X_BOTTOM_RIGHT: usize = 14;
pub const PATH_SCYTHE_LEFT: usize = 15;
pub const PATH_COBRA: usize = 16;
pub const PATH_SINE_LEFT: usize = 17;
pub const PATH_SINE_RIGHT: usize = 18;
pub const PATH_CASCADE_LEFT: usize = 19;
pub const PATH_CASCADE_RIGHT: usize = 20;
pub const PATH_CLOVERLEAF_LEFT: usize = 21;
pub const PATH_CLOVERLEAF_RIGHT: usize = 22;
pub const PATH_GEODESIC_SLINGSHOT_LEFT: usize = 23;
pub const PATH_GEODESIC_SLINGSHOT_RIGHT: usize = 24;

/// Resampled arrival splines, indexed by `ConvoySpec::path_id`.
pub fn paths() -> &'static [Vec<Vec2f>] {
    static PATHS: OnceLock<Vec<Vec<Vec2f>>> = OnceLock::new();
    PATHS.get_or_init(|| {
        vec![
            generate_smooth_spline(HANG_LEFT_CTRL),
            generate_smooth_spline(HANG_RIGHT_CTRL),
            generate_smooth_spline(VALKYRIE_CTRL),
            generate_smooth_spline(ACROBATIC_LEFT_CTRL),
            generate_smooth_spline(ACROBATIC_RIGHT_CTRL),
            generate_smooth_spline(HYPERBOLA_CTRL),
            generate_smooth_spline(BUTTERFLY_LEFT_CTRL),
            generate_smooth_spline(BUTTERFLY_RIGHT_CTRL),
            generate_smooth_spline(HELIX_LEFT_CTRL),
            generate_smooth_spline(HELIX_RIGHT_CTRL),
            generate_smooth_spline(HORSESHOE_CTRL),
            generate_smooth_spline(BALLET_X_TOP_LEFT_CTRL),
            generate_smooth_spline(BALLET_X_TOP_RIGHT_CTRL),
            generate_smooth_spline(BALLET_X_BOTTOM_LEFT_CTRL),
            generate_smooth_spline(BALLET_X_BOTTOM_RIGHT_CTRL),
            generate_smooth_spline(SCYTHE_LEFT_CTRL),
            generate_smooth_spline(COBRA_CTRL),
            generate_smooth_spline(SINE_LEFT_CTRL),
            generate_smooth_spline(SINE_RIGHT_CTRL),
            generate_smooth_spline(CASCADE_LEFT_CTRL),
            generate_smooth_spline(CASCADE_RIGHT_CTRL),
            generate_smooth_spline(CLOVERLEAF_LEFT_CTRL),
            generate_smooth_spline(CLOVERLEAF_RIGHT_CTRL),
            generate_smooth_spline(GEODESIC_SLINGSHOT_LEFT_CTRL),
            generate_smooth_spline(GEODESIC_SLINGSHOT_RIGHT_CTRL),
        ]
    })
}

// -------------------------------------------------------- convoy tables

const CD1: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 0,
        texture_id: TextureId::Alien1,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_HANG_LEFT,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien1,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_HANG_RIGHT,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien1,
        convoy_size_pc: 50,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_VALKYRIE,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien1,
        convoy_size_pc: 40,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 20,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_RIGHT,
    },
];

const CD2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 200,
        texture_id: TextureId::Alien2,
        convoy_size_pc: 50,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_VALKYRIE,
    },
    ConvoySpec {
        wait: 140,
        texture_id: TextureId::Alien2,
        convoy_size_pc: 50,
        x_mirror: true,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_VALKYRIE,
    },
    ConvoySpec {
        wait: 130,
        texture_id: TextureId::Alien2,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
    ConvoySpec {
        wait: 110,
        texture_id: TextureId::Alien2,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_RIGHT,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 30,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_HANG_LEFT,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 20,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_HANG_RIGHT,
    },
];

const CD3: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 200,
        texture_id: TextureId::Alien3,
        convoy_size_pc: 50,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_HYPERBOLA,
    },
    ConvoySpec {
        wait: 170,
        texture_id: TextureId::Alien3,
        convoy_size_pc: 50,
        x_mirror: true,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_HYPERBOLA,
    },
    ConvoySpec {
        wait: 150,
        texture_id: TextureId::Alien3,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_LEFT,
    },
    ConvoySpec {
        wait: 130,
        texture_id: TextureId::Alien7,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_RIGHT,
    },
];

const CD4: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 180,
        texture_id: TextureId::Alien4,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_HELIX_LEFT,
    },
    ConvoySpec {
        wait: 140,
        texture_id: TextureId::Alien4,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_HELIX_RIGHT,
    },
    ConvoySpec {
        wait: 130,
        texture_id: TextureId::Alien4,
        convoy_size_pc: 50,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_VALKYRIE,
    },
    ConvoySpec {
        wait: 110,
        texture_id: TextureId::Alien4,
        convoy_size_pc: 40,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
    ConvoySpec {
        wait: 100,
        texture_id: TextureId::Alien4,
        convoy_size_pc: 30,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_RIGHT,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 30,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_HORSESHOE,
    },
];

const CD5: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 0,
        texture_id: TextureId::Alien5,
        convoy_size_pc: 35,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 1,
        path_id: PATH_BALLET_X_TOP_LEFT,
    },
    ConvoySpec {
        wait: 0,
        texture_id: TextureId::Alien5,
        convoy_size_pc: 35,
        x_mirror: false,
        split: false,
        simultaneous: true,
        dock_align: 2,
        path_id: PATH_BALLET_X_TOP_RIGHT,
    },
    ConvoySpec {
        wait: 0,
        texture_id: TextureId::Alien5,
        convoy_size_pc: 35,
        x_mirror: false,
        split: false,
        simultaneous: true,
        dock_align: 1,
        path_id: PATH_BALLET_X_BOTTOM_LEFT,
    },
    ConvoySpec {
        wait: 0,
        texture_id: TextureId::Alien5,
        convoy_size_pc: 35,
        x_mirror: false,
        split: false,
        simultaneous: true,
        dock_align: 2,
        path_id: PATH_BALLET_X_BOTTOM_RIGHT,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 30,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_VALKYRIE,
    },
];

const CD6: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 190,
        texture_id: TextureId::Alien6,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_LEFT,
    },
    ConvoySpec {
        wait: 130,
        texture_id: TextureId::Alien6,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_RIGHT,
    },
    ConvoySpec {
        wait: 120,
        texture_id: TextureId::Alien6,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 100,
        texture_id: TextureId::Alien6,
        convoy_size_pc: 45,
        x_mirror: true,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien6,
        convoy_size_pc: 30,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_VALKYRIE,
    },
    ConvoySpec {
        wait: 110,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 20,
        x_mirror: true,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_VALKYRIE,
    },
];

const CD7: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 190,
        texture_id: TextureId::Alien7,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_COBRA,
    },
    ConvoySpec {
        wait: 130,
        texture_id: TextureId::Alien7,
        convoy_size_pc: 45,
        x_mirror: true,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_COBRA,
    },
    ConvoySpec {
        wait: 110,
        texture_id: TextureId::Alien7,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
    ConvoySpec {
        wait: 100,
        texture_id: TextureId::Alien7,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_RIGHT,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 40,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_VALKYRIE,
    },
];

const CD8: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 190,
        texture_id: TextureId::Alien8,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SINE_LEFT,
    },
    ConvoySpec {
        wait: 120,
        texture_id: TextureId::Alien8,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SINE_RIGHT,
    },
    ConvoySpec {
        wait: 60,
        texture_id: TextureId::Alien8,
        convoy_size_pc: 25,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_CASCADE_LEFT,
    },
    ConvoySpec {
        wait: 30,
        texture_id: TextureId::Alien8,
        convoy_size_pc: 25,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_CASCADE_RIGHT,
    },
    ConvoySpec {
        wait: 50,
        texture_id: TextureId::Alien2,
        convoy_size_pc: 30,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_HORSESHOE,
    },
];

const CD9: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 180,
        texture_id: TextureId::Alien9,
        convoy_size_pc: 40,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_CLOVERLEAF_LEFT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien9,
        convoy_size_pc: 40,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_CLOVERLEAF_RIGHT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien9,
        convoy_size_pc: 40,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien9,
        convoy_size_pc: 40,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_RIGHT,
    },
    ConvoySpec {
        wait: 70,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 30,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_VALKYRIE,
    },
];

const CD10: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 180,
        texture_id: TextureId::Alien10,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien10,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_RIGHT,
    },
    ConvoySpec {
        wait: 85,
        texture_id: TextureId::Alien10,
        convoy_size_pc: 40,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 85,
        texture_id: TextureId::Alien10,
        convoy_size_pc: 40,
        x_mirror: true,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien10,
        convoy_size_pc: 35,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_LEFT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien15,
        convoy_size_pc: 35,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_RIGHT,
    },
];

const CD11: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 190,
        texture_id: TextureId::Alien11,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SINE_LEFT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien11,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SINE_RIGHT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien11,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_RIGHT,
    },
];

const CD12: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 190,
        texture_id: TextureId::Alien12,
        convoy_size_pc: 25,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_CASCADE_LEFT,
    },
    ConvoySpec {
        wait: 20,
        texture_id: TextureId::Alien12,
        convoy_size_pc: 25,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_CASCADE_RIGHT,
    },
    ConvoySpec {
        wait: 50,
        texture_id: TextureId::Alien12,
        convoy_size_pc: 35,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 50,
        texture_id: TextureId::Alien12,
        convoy_size_pc: 35,
        x_mirror: true,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 50,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 25,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_LEFT,
    },
];

const CD13: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_VALKYRIE,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien1,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien2,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_RIGHT,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 45,
        x_mirror: true,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien4,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_LEFT,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien5,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_RIGHT,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_COBRA,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_HANG_LEFT,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_HANG_RIGHT,
    },
];

const CD14: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 0,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_GEODESIC_SLINGSHOT_LEFT,
    },
    ConvoySpec {
        wait: 40,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_GEODESIC_SLINGSHOT_RIGHT,
    },
    ConvoySpec {
        wait: 60,
        texture_id: TextureId::Alien2,
        convoy_size_pc: 40,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien3,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
    ConvoySpec {
        wait: 60,
        texture_id: TextureId::Alien4,
        convoy_size_pc: 40,
        x_mirror: true,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
];

const CD15: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 170,
        texture_id: TextureId::Alien15,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien15,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_RIGHT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 40,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien12,
        convoy_size_pc: 40,
        x_mirror: true,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 35,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_VALKYRIE,
    },
];

const CD1_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 0,
        texture_id: TextureId::Alien1,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien1,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_RIGHT,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien1,
        convoy_size_pc: 50,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 70,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 25,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_GEODESIC_SLINGSHOT_LEFT,
    },
];

const CD2_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 160,
        texture_id: TextureId::Alien2,
        convoy_size_pc: 50,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_LEFT,
    },
    ConvoySpec {
        wait: 110,
        texture_id: TextureId::Alien2,
        convoy_size_pc: 50,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_RIGHT,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien2,
        convoy_size_pc: 45,
        x_mirror: true,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 20,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
];

const CD3_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 170,
        texture_id: TextureId::Alien3,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_CLOVERLEAF_LEFT,
    },
    ConvoySpec {
        wait: 110,
        texture_id: TextureId::Alien3,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_CLOVERLEAF_RIGHT,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien3,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SINE_LEFT,
    },
];

const CD4_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 150,
        texture_id: TextureId::Alien4,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_GEODESIC_SLINGSHOT_LEFT,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien4,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_GEODESIC_SLINGSHOT_RIGHT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien4,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
    ConvoySpec {
        wait: 70,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 20,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_RIGHT,
    },
];

const CD5_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 0,
        texture_id: TextureId::Alien5,
        convoy_size_pc: 35,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 1,
        path_id: PATH_BALLET_X_TOP_LEFT,
    },
    ConvoySpec {
        wait: 0,
        texture_id: TextureId::Alien5,
        convoy_size_pc: 35,
        x_mirror: false,
        split: false,
        simultaneous: true,
        dock_align: 2,
        path_id: PATH_BALLET_X_TOP_RIGHT,
    },
    ConvoySpec {
        wait: 0,
        texture_id: TextureId::Alien5,
        convoy_size_pc: 35,
        x_mirror: false,
        split: false,
        simultaneous: true,
        dock_align: 1,
        path_id: PATH_BALLET_X_BOTTOM_LEFT,
    },
    ConvoySpec {
        wait: 0,
        texture_id: TextureId::Alien5,
        convoy_size_pc: 35,
        x_mirror: false,
        split: false,
        simultaneous: true,
        dock_align: 2,
        path_id: PATH_BALLET_X_BOTTOM_RIGHT,
    },
];

const CD6_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 150,
        texture_id: TextureId::Alien6,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_CLOVERLEAF_RIGHT,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien6,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_CLOVERLEAF_LEFT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien6,
        convoy_size_pc: 40,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_VALKYRIE,
    },
    ConvoySpec {
        wait: 70,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 20,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
];

const CD7_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 150,
        texture_id: TextureId::Alien7,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
    ConvoySpec {
        wait: 90,
        texture_id: TextureId::Alien7,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_RIGHT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien7,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_COBRA,
    },
];

const CD8_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 150,
        texture_id: TextureId::Alien8,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_LEFT,
    },
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien8,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_RIGHT,
    },
    ConvoySpec {
        wait: 70,
        texture_id: TextureId::Alien8,
        convoy_size_pc: 30,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
];

const CD9_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 140,
        texture_id: TextureId::Alien9,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_GEODESIC_SLINGSHOT_LEFT,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien9,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_GEODESIC_SLINGSHOT_RIGHT,
    },
    ConvoySpec {
        wait: 70,
        texture_id: TextureId::Alien9,
        convoy_size_pc: 40,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_CLOVERLEAF_LEFT,
    },
];

const CD10_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 150,
        texture_id: TextureId::Alien10,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien10,
        convoy_size_pc: 45,
        x_mirror: true,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 70,
        texture_id: TextureId::Alien10,
        convoy_size_pc: 40,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_LEFT,
    },
    ConvoySpec {
        wait: 70,
        texture_id: TextureId::Alien10,
        convoy_size_pc: 40,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_RIGHT,
    },
];

const CD11_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 150,
        texture_id: TextureId::Alien11,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_CLOVERLEAF_LEFT,
    },
    ConvoySpec {
        wait: 75,
        texture_id: TextureId::Alien11,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_CLOVERLEAF_RIGHT,
    },
    ConvoySpec {
        wait: 70,
        texture_id: TextureId::Alien11,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_LEFT,
    },
];

const CD12_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 150,
        texture_id: TextureId::Alien12,
        convoy_size_pc: 35,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 60,
        texture_id: TextureId::Alien12,
        convoy_size_pc: 35,
        x_mirror: true,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 50,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 30,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_ACROBATIC_RIGHT,
    },
];

const CD13_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 80,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_GEODESIC_SLINGSHOT_LEFT,
    },
    ConvoySpec {
        wait: 60,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_GEODESIC_SLINGSHOT_RIGHT,
    },
    ConvoySpec {
        wait: 60,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
];

const CD14_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 0,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_GEODESIC_SLINGSHOT_LEFT,
    },
    ConvoySpec {
        wait: 35,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_GEODESIC_SLINGSHOT_RIGHT,
    },
    ConvoySpec {
        wait: 50,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 50,
        texture_id: TextureId::Alien12,
        convoy_size_pc: 45,
        x_mirror: false,
        split: false,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_LEFT,
    },
    ConvoySpec {
        wait: 50,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 45,
        x_mirror: true,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
];

const CD15_S2: &[ConvoySpec] = &[
    ConvoySpec {
        wait: 140,
        texture_id: TextureId::Alien15,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_GEODESIC_SLINGSHOT_LEFT,
    },
    ConvoySpec {
        wait: 70,
        texture_id: TextureId::Alien14,
        convoy_size_pc: 45,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_GEODESIC_SLINGSHOT_RIGHT,
    },
    ConvoySpec {
        wait: 60,
        texture_id: TextureId::Alien13,
        convoy_size_pc: 40,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_SCYTHE_LEFT,
    },
    ConvoySpec {
        wait: 60,
        texture_id: TextureId::Alien15,
        convoy_size_pc: 40,
        x_mirror: false,
        split: true,
        simultaneous: false,
        dock_align: 0,
        path_id: PATH_BUTTERFLY_LEFT,
    },
];

static TABLES_S1: [&'static [ConvoySpec]; 15] = [
    CD1, CD2, CD3, CD4, CD5, CD6, CD7, CD8, CD9, CD10, CD11, CD12, CD13, CD14, CD15,
];
static TABLES_S2: [&'static [ConvoySpec]; 15] = [
    CD1_S2, CD2_S2, CD3_S2, CD4_S2, CD5_S2, CD6_S2, CD7_S2, CD8_S2, CD9_S2, CD10_S2, CD11_S2, CD12_S2, CD13_S2, CD14_S2, CD15_S2,
];

/// LevelData::GetConvoyData -- `usize` mirrors the C++ `size_t` parameter
/// (a negative level at a call site wraps identically in both languages).
/// Level 0 clamps to wave 1; levels wrap every 15 waves; stage >= 2 uses
/// the stage-2 tables.
pub fn get_convoy_data(level: usize) -> &'static [ConvoySpec] {
    let level = if level == 0 { 1 } else { level };
    let idx = (level - 1) % WAVES_PER_STAGE as usize;
    if wave_to_stage(level as i32) >= 2 {
        TABLES_S2[idx]
    } else {
        TABLES_S1[idx]
    }
}

/// LevelData::GetTotalLevels -- waves per stage cycle (15).
pub fn get_total_levels() -> usize {
    WAVES_PER_STAGE as usize
}
