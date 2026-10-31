//! Port of the pure frame-pacing policies of src/core/frame_pacing.h.
//! Constants mirror GameRules::Simulation; golden vectors pin every branch.

pub const FIXED_STEP_NS: u64 = 16_666_667;
pub const MAX_FRAME_DELTA_NS: u64 = 250_000_000;
pub const MAX_ACCUMULATOR_NS: u64 = FIXED_STEP_NS * 2;
pub const JITTER_SLACK_NS: u64 = 250_000;
pub const MAX_STEPS_PER_FRAME: i32 = 2;
pub const HITCH_DELTA_NS: u64 = FIXED_STEP_NS * 3;

pub fn is_hitch(raw_delta_ns: u64) -> bool {
    raw_delta_ns >= HITCH_DELTA_NS
}

/// A hitch is charged as ONE nominal step; ordinary jitter is caught up.
pub fn simulated_delta(raw_delta_ns: u64) -> u64 {
    if is_hitch(raw_delta_ns) {
        return FIXED_STEP_NS;
    }
    raw_delta_ns.min(MAX_FRAME_DELTA_NS)
}

pub fn accumulate_bounded(accumulator_ns: u64, raw_delta_ns: u64) -> u64 {
    (accumulator_ns + simulated_delta(raw_delta_ns)).min(MAX_ACCUMULATOR_NS)
}

/// Only the FIRST step of a frame enjoys the VBlank jitter slack.
pub fn step_threshold(steps_taken_this_frame: i32) -> u64 {
    if steps_taken_this_frame == 0 && FIXED_STEP_NS > JITTER_SLACK_NS {
        FIXED_STEP_NS - JITTER_SLACK_NS
    } else {
        FIXED_STEP_NS
    }
}

pub fn consume_step(accumulator_ns: u64) -> u64 {
    if accumulator_ns >= FIXED_STEP_NS {
        accumulator_ns - FIXED_STEP_NS
    } else {
        0
    }
}

pub fn interpolation_alpha(accumulator_ns: u64) -> f32 {
    (accumulator_ns as f64 / FIXED_STEP_NS as f64) as f32
}
