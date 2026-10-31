//! Port of src/simulation/trajectory.{h,cc}: the per-starship kinematic state
//! machine (arriving -> joining -> cruising <-> attacking). Every stage is
//! arithmetic + sqrt except the joining arc (atan2/cos/sin), whose ulps are
//! libm-version dependent -- golden vectors compare those with a tolerance
//! while the stage sequence stays exact (RUST.md policy).

use crate::formation::{self, GridData};
use crate::mathx::fast_round_f32;
use crate::path::{generate_smooth_spline, Vec2f};
use crate::random_stream::RandomStream;

pub const PI: f32 = std::f32::consts::PI;

// Mirrors of GameRules::Fleet constants (golden vectors police this copy).
const ATTACK_DIVE_SPEED_MULTIPLIER: f32 = 0.90;
const ATTACK_DIVE_MIN_VELOCITY_PX: f32 = 1.5;
const ATTACK_DIVE_LATERAL_INERTIA: f32 = 0.55;
const ATTACK_DIVE_LATERAL_JITTER_UNITS: i32 = 110;
const ATTACK_DIVE_LATERAL_SWING_CAP: f32 = 150.0;
const ATTACK_DIVE_MIN_ADVANCE_UNITS: f32 = 1024.0 / 26.0;
const ATTACK_DIVE_ADVANCE_JITTER_UNITS: i32 = 159;
const ATTACK_DIVE_EXIT_NORMALIZED_Y: f32 = 1050.0;
const MAX_RAW_ATTACK_POINTS: usize = 48;
const JOIN_DIRECT_DOCK_VELOCITY_FACTOR: f32 = 4.0;
const JOIN_HEADING_TURN_RATE_DEG: f32 = 3.5;
const ARRIVAL_MARGIN_FACTOR: f32 = 1.15;
const KAMIKAZE_MIDPOINT_RATIO: f32 = 0.45;
const KAMIKAZE_LATERAL_JITTER_UNITS: i32 = 50;
const KAMIKAZE_OVERSHOOT_UNITS: f32 = 140.0;

/// Discriminants match the C++ `Trajectory::stage_t` declaration order.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Stage {
    Arriving = 0,
    Joining = 1,
    Cruising = 2,
    Attacking = 3,
}

#[derive(Clone, Copy)]
pub struct ViewportMetrics {
    pub win_w: i32,
    pub win_h: i32,
    pub scale: f32,
}

pub struct Trajectory {
    stage: Stage,
    arrival: Vec<Vec2f>,
    arrival_idx: usize,
    mirrored: bool,
    join_heading: f32,
    base_cruise: (i32, i32),
    metrics: ViewportMetrics,
    grid_col: i32,
    grid_row: i32,
    rng: RandomStream,
    attack: Vec<Vec2f>,
    attack_idx: usize,
    stage_cycle: i32,
    #[allow(dead_code)]
    used_cols: i32,
    grid: GridData,
}

impl Trajectory {
    /// Reconfigure-equivalent construction: same field order and seed remap.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        arrival: &[Vec2f],
        mirrored: bool,
        base_cruise: (i32, i32),
        metrics: ViewportMetrics,
        grid_col: i32,
        grid_row: i32,
        random_seed: u32,
        stage_cycle: i32,
        used_cols: i32,
    ) -> Self {
        Self {
            stage: Stage::Arriving,
            arrival: arrival.to_vec(),
            arrival_idx: 0,
            mirrored,
            join_heading: PI * 0.5,
            base_cruise,
            metrics,
            grid_col,
            grid_row,
            rng: RandomStream::new(random_seed),
            attack: Vec::new(),
            attack_idx: 0,
            stage_cycle,
            used_cols: used_cols.max(1),
            // The C++ twin reads the global formation; the dump fixes it at
            // scale 1.0, so the port snapshots exactly that state.
            grid: formation::recompute(1.0),
        }
    }

    pub fn stage(&self) -> Stage {
        self.stage
    }

    pub fn attack_point_count(&self) -> usize {
        self.attack.len()
    }

    pub fn init_position(&self) -> (i32, i32) {
        let pt = self.arrival[0];
        let raw_x = if self.mirrored { 1024.0 - pt.x } else { pt.x };
        let raw_y = pt.y;
        let win_w = self.metrics.win_w as f32;
        let win_h = self.metrics.win_h as f32;
        let sx = (raw_x * win_w) / 1024.0;
        let sy = (raw_y * win_h) / 1024.0;
        let mut px = fast_round_f32(sx);
        let mut py = fast_round_f32(sy);
        let margin_y = (self.grid.height as f32 * ARRIVAL_MARGIN_FACTOR) as i32;
        let margin_x = (self.grid.width as f32 * ARRIVAL_MARGIN_FACTOR) as i32;
        if py < 0 {
            py = -margin_y;
        } else if py >= self.metrics.win_h {
            py = self.metrics.win_h + margin_y;
        }
        if px < 0 {
            px = -margin_x;
        } else if px >= self.metrics.win_w {
            px = self.metrics.win_w + margin_x;
        }
        (px, py)
    }

    pub fn cruise_target(&self) -> (i32, i32) {
        let slot = formation::get_slot(&self.grid, self.stage_cycle, self.grid_row, self.grid_col);
        (
            self.base_cruise.0 + slot.offset_x,
            self.base_cruise.1 + slot.offset_y,
        )
    }

    fn arrival_end_heading_rad(&self) -> f32 {
        let count = self.arrival.len();
        let last = self.arrival[count - 1];
        let prev = self.arrival[if count >= 2 { count - 2 } else { count - 1 }];
        let win_w = self.metrics.win_w as f32;
        let win_h = self.metrics.win_h as f32;
        let mirror_x = |raw: f32| -> f32 {
            ((if self.mirrored { 1024.0 - raw } else { raw }) * win_w) / 1024.0
        };
        let dx = mirror_x(last.x) - mirror_x(prev.x);
        let dy = ((last.y - prev.y) * win_h) / 1024.0;
        if dx == 0.0 && dy == 0.0 {
            return PI * 0.5;
        }
        dy.atan2(dx)
    }

    fn begin_docking_arc(&mut self, heading_rad: f32) {
        self.stage = Stage::Joining;
        self.join_heading = heading_rad;
    }

    /// Faithful twin of NextPositionF, recursion included (waypoint leftover
    /// budget re-entry). Returns the new (x, y).
    pub fn next_position_f(&mut self, from_x: f32, from_y: f32, velocity: f32) -> (f32, f32) {
        if velocity <= 0.0 {
            return (from_x, from_y);
        }
        match self.stage {
            Stage::Arriving => {
                if self.arrival_idx >= self.arrival.len() {
                    let heading = self.arrival_end_heading_rad();
                    self.begin_docking_arc(heading);
                    return self.next_position_f(from_x, from_y, velocity);
                }
                let win_w = self.metrics.win_w as f32;
                let win_h = self.metrics.win_h as f32;
                let raw_pt = self.arrival[self.arrival_idx];
                let goal_rx = if self.mirrored { 1024.0 - raw_pt.x } else { raw_pt.x };
                let goal_x = (goal_rx * win_w) / 1024.0;
                let goal_y = (raw_pt.y * win_h) / 1024.0;
                let dx = goal_x - from_x;
                let dy = goal_y - from_y;
                let len = (dx * dx + dy * dy).sqrt();
                if len <= velocity {
                    let remaining = velocity - len;
                    self.arrival_idx += 1;
                    if self.arrival_idx >= self.arrival.len() {
                        let heading = self.arrival_end_heading_rad();
                        self.begin_docking_arc(heading);
                    }
                    if remaining > 0.001 {
                        return self.next_position_f(goal_x, goal_y, remaining);
                    }
                    return (goal_x, goal_y);
                }
                (from_x + (dx / len) * velocity, from_y + (dy / len) * velocity)
            }
            Stage::Joining => {
                let target = self.cruise_target();
                let dx = target.0 as f32 - from_x;
                let dy = target.1 as f32 - from_y;
                let len = (dx * dx + dy * dy).sqrt();
                if len <= velocity * JOIN_DIRECT_DOCK_VELOCITY_FACTOR {
                    if len <= velocity {
                        self.stage = Stage::Cruising;
                        return (target.0 as f32, target.1 as f32);
                    }
                    let inv_len = 1.0 / len;
                    let out = (from_x + dx * inv_len * velocity, from_y + dy * inv_len * velocity);
                    self.join_heading = dy.atan2(dx);
                    return out;
                }
                let max_turn = JOIN_HEADING_TURN_RATE_DEG * (PI / 180.0);
                let mut diff = dy.atan2(dx) - self.join_heading;
                while diff > PI {
                    diff -= 2.0 * PI;
                }
                while diff < -PI {
                    diff += 2.0 * PI;
                }
                self.join_heading += diff.clamp(-max_turn, max_turn);
                (
                    from_x + self.join_heading.cos() * velocity,
                    from_y + self.join_heading.sin() * velocity,
                )
            }
            Stage::Cruising => {
                let target = self.cruise_target();
                let dx = target.0 as f32 - from_x;
                let dy = target.1 as f32 - from_y;
                let len = (dx * dx + dy * dy).sqrt();
                if len <= velocity {
                    (target.0 as f32, target.1 as f32)
                } else {
                    let inv_len = 1.0 / len;
                    (from_x + dx * inv_len * velocity, from_y + dy * inv_len * velocity)
                }
            }
            Stage::Attacking => {
                if self.attack_idx >= self.attack.len() {
                    self.attack.clear();
                    self.attack_idx = 0;
                    self.begin_docking_arc(PI * 0.5);
                    return (from_x, -(self.grid.height as f32));
                }
                let win_w = self.metrics.win_w as f32;
                let win_h = self.metrics.win_h as f32;
                let raw_pt = self.attack[self.attack_idx];
                let goal_x = (raw_pt.x * win_w) / 1024.0;
                let goal_y = (raw_pt.y * win_h) / 1024.0;
                let dx = goal_x - from_x;
                let dy = goal_y - from_y;
                let len = (dx * dx + dy * dy).sqrt();
                let eff_vel =
                    (ATTACK_DIVE_MIN_VELOCITY_PX.max(velocity * ATTACK_DIVE_SPEED_MULTIPLIER))
                        .min(velocity);
                if len <= eff_vel {
                    let remaining = eff_vel - len;
                    self.attack_idx += 1;
                    if self.attack_idx >= self.attack.len() {
                        self.attack.clear();
                        self.attack_idx = 0;
                        self.begin_docking_arc(PI * 0.5);
                        return (from_x, -(self.grid.height as f32));
                    }
                    if remaining > 0.001 {
                        return self.next_position_f(goal_x, goal_y, remaining);
                    }
                    return (goal_x, goal_y);
                }
                (from_x + (dx / len) * eff_vel, from_y + (dy / len) * eff_vel)
            }
        }
    }

    /// Twin of BuildAttack: identical RNG draw order (jitter then advance per
    /// control point), identical 48-point cap and exit condition.
    pub fn build_attack(&mut self, from: (i32, i32)) {
        self.attack.clear();
        self.attack_idx = 0;

        let mut raw = [Vec2f::new(0.0, 0.0); MAX_RAW_ATTACK_POINTS];
        let mut raw_count: usize = 0;
        let min_delta_y = ATTACK_DIVE_MIN_ADVANCE_UNITS;
        let win_w = self.metrics.win_w.max(1) as f32;
        let win_h = self.metrics.win_h.max(1) as f32;
        let mut next =
            Vec2f::new((from.0 as f32 * 1024.0) / win_w, (from.1 as f32 * 1024.0) / win_h);
        raw[raw_count] = next;
        raw_count += 1;

        let mut prev_off_x = 0.0f32;
        loop {
            let jitter_x = self
                .rng
                .uniform_int(-ATTACK_DIVE_LATERAL_JITTER_UNITS, ATTACK_DIVE_LATERAL_JITTER_UNITS)
                as f32;
            let off_x = (prev_off_x * ATTACK_DIVE_LATERAL_INERTIA + jitter_x)
                .clamp(-ATTACK_DIVE_LATERAL_SWING_CAP, ATTACK_DIVE_LATERAL_SWING_CAP);
            prev_off_x = off_x;
            let off_y = self.rng.uniform_int(0, ATTACK_DIVE_ADVANCE_JITTER_UNITS) as f32 + min_delta_y;
            next.x += off_x;
            next.y += off_y;
            if raw_count >= MAX_RAW_ATTACK_POINTS {
                break;
            }
            raw[raw_count] = next;
            raw_count += 1;
            if !(next.y < ATTACK_DIVE_EXIT_NORMALIZED_Y) {
                break;
            }
        }

        self.attack = generate_smooth_spline(&raw[..raw_count]);
        self.stage = Stage::Attacking;
    }

    /// Twin of BuildKamikazeDive: same midpoint ratio, same asymmetric jitter
    /// draw (-J, J-1) and same overshoot past the player.
    pub fn build_kamikaze_dive(&mut self, from: (i32, i32), target_player: (i32, i32)) {
        self.attack.clear();
        self.attack_idx = 0;

        let win_w = self.metrics.win_w.max(1) as f32;
        let win_h = self.metrics.win_h.max(1) as f32;
        let start =
            Vec2f::new((from.0 as f32 * 1024.0) / win_w, (from.1 as f32 * 1024.0) / win_h);
        let target = Vec2f::new(
            (target_player.0 as f32 * 1024.0) / win_w,
            (target_player.1 as f32 * 1024.0) / win_h,
        );
        let mid_x = start.x + (target.x - start.x) * KAMIKAZE_MIDPOINT_RATIO
            + self
                .rng
                .uniform_int(-KAMIKAZE_LATERAL_JITTER_UNITS, KAMIKAZE_LATERAL_JITTER_UNITS - 1)
                as f32;
        let mid_y = start.y + (target.y - start.y) * KAMIKAZE_MIDPOINT_RATIO;
        let raw = [
            start,
            Vec2f::new(mid_x, mid_y),
            target,
            Vec2f::new(target.x, target.y + KAMIKAZE_OVERSHOOT_UNITS),
        ];
        self.attack = generate_smooth_spline(&raw);
        self.stage = Stage::Attacking;
    }
}
