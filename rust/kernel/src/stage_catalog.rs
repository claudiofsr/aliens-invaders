//! Port of src/entities/stage_catalog.h plus the Fleet/Player progression
//! helpers it consumes. Every float expression mirrors the C++ operation
//! order so the golden vectors match bit for bit.

pub const WAVES_PER_STAGE: i32 = 15;
pub const WANDERER_MAX_WAIT: i32 = 180;

// Mirrors of GameRules constants (RUST.md rule 4: the golden vectors watch
// this duplication; C++ stays canonical until Phase 7).
const COUNTS_S1: [i32; 15] = [5, 6, 4, 6, 5, 6, 5, 5, 5, 6, 4, 5, 10, 5, 5];
const COUNTS_S2: [i32; 15] = [4, 4, 3, 4, 4, 4, 3, 3, 3, 4, 3, 3, 3, 5, 4];
const MAX_BOMBS_PER_STAGE: i32 = 10;
const BASE_BOMBS_PER_STAGE: i32 = 4;
const BOMBS_PER_STAGE_LEVEL_DIVISOR: i32 = 3;
const CYCLE_BOMB_MULT_BASE: f32 = 1.40;
const CYCLE_BOMB_MULT_MAX: f32 = 1.65;
const CYCLE_BOMB_MULT_INCREMENT: f32 = 0.08;
const STAGE_SPEEDS: [(f32, f32); 5] =
    [(5.0, 7.5), (8.0, 10.5), (11.0, 13.5), (14.0, 16.5), (17.0, 20.0)];
const STAGE_MAX_ATTACK_WAIT: [i32; 5] = [300, 240, 180, 120, 60];
const PLAYER_BASE_SPEED: f32 = 3.5;
const PLAYER_SPEED_BOOST_PERCENT: f32 = 0.30;
const PLAYER_MAX_SPEED_LEVEL: i32 = 5;
const BASE_FIRE_INTERVAL_FRAMES: f32 = 40.0;
const MIN_FIRE_INTERVAL_FRAMES: i32 = 8;
const PLAYER_MAX_FIRE_LEVEL: i32 = 2;
const FIRE_RATE_BOOST_PERCENT: f32 = 0.10;

/// Progression::WaveToStage -- 1-based wave to 1-based stage cycle.
pub fn wave_to_stage(wave: i32) -> i32 {
    (wave - 1) / WAVES_PER_STAGE + 1
}

/// Progression::WaveInStage -- 0-based offset inside the stage cycle.
pub fn wave_in_stage(wave: i32) -> i32 {
    (wave - 1) % WAVES_PER_STAGE
}

/// Fleet::ComputeSpeed -- intra-stage interpolation between tier bounds.
pub fn fleet_compute_speed(level: i32, max_levels: i32) -> f32 {
    let stage = wave_to_stage(level);
    let intra = wave_in_stage(level) as f32 / max_levels.max(1) as f32;
    let (lo, hi) = STAGE_SPEEDS[(stage - 1).clamp(0, 4) as usize];
    lo + intra * (hi - lo)
}

pub fn get_max_attack_wait_frames_for_stage(stage: i32) -> i32 {
    STAGE_MAX_ATTACK_WAIT[(stage - 1).clamp(0, 4) as usize]
}

pub fn get_max_attack_wait_frames(level: i32) -> i32 {
    get_max_attack_wait_frames_for_stage(wave_to_stage(level))
}

pub fn get_kamikaze_quota(level: i32) -> i32 {
    wave_to_stage(level)
}

/// Player::ComputeSpeed -- thruster tier scaling.
pub fn player_compute_speed(speed_level: i32) -> f32 {
    let clamped = speed_level.clamp(0, PLAYER_MAX_SPEED_LEVEL);
    PLAYER_BASE_SPEED * (1.0 + PLAYER_SPEED_BOOST_PERCENT * clamped as f32)
}

/// Player::ComputeFireInterval -- round-half-up of the rate-scaled interval.
pub fn player_compute_fire_interval(fire_level: i32) -> i32 {
    let clamped = fire_level.clamp(0, PLAYER_MAX_FIRE_LEVEL);
    let rate_mult = 1.0f32 + FIRE_RATE_BOOST_PERCENT * clamped as f32;
    let frames = (BASE_FIRE_INTERVAL_FRAMES / rate_mult + 0.5) as i32;
    MIN_FIRE_INTERVAL_FRAMES.max(frames)
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct StageProfile {
    pub wave: i32,
    pub stage_cycle: i32,
    pub convoy_count: i32,
    pub max_bombs: i32,
    pub cycle_bomb_mult: f32,
    pub fleet_speed: f32,
    pub max_attack_wait_frames: i32,
    pub kamikaze_quota: i32,
    pub is_radioactive_wave: bool,
    pub is_relativistic_wave: bool,
    pub wanderers_allowed: bool,
}

/// StageCatalog::GetProfile -- compile-time wave descriptor, same formulas.
pub fn get_profile(wave: i32, max_levels: i32) -> StageProfile {
    let clamped_wave = wave.max(1);
    let cycle = wave_to_stage(clamped_wave);
    let lvl_idx = ((clamped_wave - 1) % WAVES_PER_STAGE) as usize;
    let convoy_count = if cycle >= 2 { COUNTS_S2[lvl_idx] } else { COUNTS_S1[lvl_idx] };
    let max_bombs =
        MAX_BOMBS_PER_STAGE.min(BASE_BOMBS_PER_STAGE + clamped_wave / BOMBS_PER_STAGE_LEVEL_DIVISOR);
    let cycle_bomb_mult = if cycle == 1 {
        1.0
    } else if cycle == 2 {
        1.25
    } else {
        CYCLE_BOMB_MULT_MAX
            .min(CYCLE_BOMB_MULT_BASE + (cycle - 3) as f32 * CYCLE_BOMB_MULT_INCREMENT)
    };
    let stage_offset = clamped_wave % WAVES_PER_STAGE;
    StageProfile {
        wave: clamped_wave,
        stage_cycle: cycle,
        convoy_count,
        max_bombs,
        cycle_bomb_mult,
        fleet_speed: fleet_compute_speed(clamped_wave, max_levels),
        max_attack_wait_frames: get_max_attack_wait_frames(clamped_wave),
        kamikaze_quota: get_kamikaze_quota(clamped_wave),
        is_radioactive_wave: stage_offset == 4,
        is_relativistic_wave: stage_offset == 14,
        wanderers_allowed: cycle >= 2,
    }
}
