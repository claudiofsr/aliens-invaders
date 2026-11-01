//! Port of the Alien starship (src/entities/actors.{h,cc}): the per-frame
//! state machine (warp transit, trajectory follow, Delta Jump Guard, angular
//! partitioning, spin roll) plus the Alien4 electrosphere/alpha-decay and the
//! Alien14 relativistic-warp special mechanics.
//!
//! Scope notes (RUST.md slice 4b):
//! - The recycle bin (AGENT 7 pool/reuse) is omitted: Respawn() rebinds every
//!   field the constructor binds and re-rolls the identical birth traits, so a
//!   recycled alien is observationally indistinguishable from a fresh one.
//! - RecordWarpGhost / warp_ghosts_ / prev_angle_total_ / InterpolatedAngle
//!   feed only the render-side afterimage trail and draw lerp; no simulation
//!   path or corpus row reads them.
//! - The `PresentationSink*` member becomes an `&mut EventSink` parameter on
//!   the two methods that emit cosmetic intents.

use crate::formation::{self, MAX_GRID_ROWS};
use crate::game_object::{advance_toward, advance_turn_deg, clamp_step_to_budget, GameObject};
use crate::mathx::{fast_round_f32, Coord};
use crate::path::Vec2f;
use crate::sink::EventSink;
use crate::textures::{dims, TextureId};
use crate::trajectory::{Stage, Trajectory};
use std::sync::OnceLock;

// GameRules constants (golden-vector policed copies of src/core/constants.h).
const FLEET_MIN_VELOCITY_PIXELS: f32 = 1.8;
const MIN_ATTACK_WAIT_FRAMES: i32 = 30;
const ALIEN_SPIN_ANIMATION_FRAMES: i32 = 46;
const ALIEN_SPIN_CHANCE_DENOMINATOR: i32 = 140;
const MAX_TURN_RATE_DEG: f32 = 5.5;
const TURN_RESPONSE_FACTOR: f32 = 0.18;
const TURN_SNAP_THRESHOLD_DEG: f32 = 0.5;
const KAMIKAZE_PULSE_PHASE_STEP: f32 = 0.08;

const ALIEN4_INITIAL_HEALTH: i32 = 2;
const ALIEN4_ELECTROSPHERE_PROBABILITY: f32 = 0.20;
const ALIEN4_ALPHA_DECAY_SCALE: f32 = 0.50;
const ALIEN4_DECAYED_SPEED_MULT: f32 = 1.30;
const ALIEN4_DECAYED_ORBIT_SPEED_MULT: f32 = 3.0;
const ALIEN4_ELECTRON_PHASE_STEP: f32 = 0.020;

const ALIEN14_WARP_EVASION_PROBABILITY: f32 = 0.60;
const ALIEN14_NUM_WARP_EVASIONS: i32 = 2;
const ALIEN14_WARP_SPEED_PIXELS: f32 = 32.0;
const ALIEN14_WARP_MAX_FRAMES: i32 = 60;
const ALIEN14_WARP_COOLDOWN_SECONDS: f32 = 1.4;
const SIMULATION_FREQUENCY_HZ: f32 = 60.0;
const ALIEN14_POST_WARP_ATTACK_WAIT_MIN_FRAMES: i32 = 180;
/// GameRules::Visuals::kFloatingTextNoticeFontSize
const FLOATING_TEXT_NOTICE_FONT_SIZE: f32 = 22.0;

/// Twin of the anonymous-namespace kSpinAngleTable: quintic ease-out of the
/// remaining spin arc, sampled per animation frame.
fn spin_angle_table() -> &'static [f32; (ALIEN_SPIN_ANIMATION_FRAMES + 1) as usize] {
    static TABLE: OnceLock<[f32; (ALIEN_SPIN_ANIMATION_FRAMES + 1) as usize]> = OnceLock::new();
    TABLE.get_or_init(|| {
        let mut tbl = [0.0f32; (ALIEN_SPIN_ANIMATION_FRAMES + 1) as usize];
        for timer in 1..=ALIEN_SPIN_ANIMATION_FRAMES {
            let progress =
                1.0 - (timer as f32 / ALIEN_SPIN_ANIMATION_FRAMES as f32);
            let t2 = progress * progress;
            let smooth = t2 * progress * (progress * (progress * 6.0 - 15.0) + 10.0);
            tbl[timer as usize] = smooth * 360.0;
        }
        tbl
    })
}

/// Alien twin. The float transform position drives the movement math; the
/// integer GameObject position (re-rounded every tick) is what collisions and
/// the corpus rows observe.
pub struct Alien {
    object: GameObject,
    trajectory: Trajectory,
    speed: f32,
    transform_pos: Vec2f,
    transform_prev: Vec2f,
    texture_id: TextureId,
    angle: f32,
    spin_angle: f32,
    spin_timer: i32,
    has_spun: bool,
    attack_wait_timer: i32,
    is_kamikaze: bool,
    is_wanderer: bool,
    has_electrosphere: bool,
    health: i32,
    is_alpha_decayed: bool,
    /// Animation-phase state (render-only evolution, kept so the twin stays a
    /// faithful record of every C++ field the tick mutates).
    kamikaze_phase: f32,
    electron_phase: f32,
    electron_offset0: f32,
    electron_offset1: f32,
    has_warp_property: bool,
    warp_evasions_remaining: i32,
    /// Decrement-only state in the corpus window (the render side reads it);
    /// kept so future golden rows can observe it without a port change.
    warp_cooldown_timer: i32,
    is_warping: bool,
    warp_frames_left: i32,
    warp_dest: Coord,
}

impl Alien {
    /// Twin of the Alien constructor: binds the trajectory snapshot, then runs
    /// the shared FinishSpawn path (combat-state reset + birth-trait rolls).
    pub fn new(trajectory: Trajectory, speed: f32, texture_id: TextureId) -> Self {
        let (w, h) = dims(texture_id);
        let init = trajectory.init_position();
        let pos = Vec2f::new(init.0 as f32, init.1 as f32);
        let mut alien = Self {
            object: GameObject::new(Coord::new(init.0, init.1), w, h),
            trajectory,
            speed,
            transform_pos: pos,
            transform_prev: pos,
            texture_id,
            angle: 0.0,
            spin_angle: 0.0,
            spin_timer: 0,
            has_spun: false,
            attack_wait_timer: 0,
            is_kamikaze: false,
            is_wanderer: false,
            has_electrosphere: false,
            health: ALIEN4_INITIAL_HEALTH,
            is_alpha_decayed: false,
            kamikaze_phase: 0.0,
            electron_phase: 0.0,
            electron_offset0: 0.0,
            electron_offset1: 0.0,
            has_warp_property: false,
            warp_evasions_remaining: 0,
            warp_cooldown_timer: 0,
            is_warping: false,
            warp_frames_left: 0,
            warp_dest: Coord::new(0, 0),
        };
        alien.finish_spawn();
        alien
    }

    /// Twin of Alien::FinishSpawn. The birth-trait draws consume the
    /// TRAJECTORY RNG in the exact C++ order (Alien4: two electron phases
    /// then the electrosphere roll; Alien14: the warp-evasion roll), so the
    /// corpus seed searches land on the same seeds on both sides.
    fn finish_spawn(&mut self) {
        if self.texture_id == TextureId::Alien4 {
            self.electron_offset0 = self.trajectory.uniform_float(0.0, 2.0 * std::f32::consts::PI);
            self.electron_offset1 = self.electron_offset0
                + self.trajectory.uniform_float(
                    std::f32::consts::PI * 0.5,
                    std::f32::consts::PI * 1.5,
                );
            self.has_electrosphere =
                self.trajectory.uniform_float(0.0, 1.0) < ALIEN4_ELECTROSPHERE_PROBABILITY;
        }
        if self.texture_id == TextureId::Alien14 {
            self.has_warp_property =
                self.trajectory.uniform_float(0.0, 1.0) < ALIEN14_WARP_EVASION_PROBABILITY;
            self.warp_evasions_remaining = if self.has_warp_property {
                ALIEN14_NUM_WARP_EVASIONS
            } else {
                0
            };
        }
    }

    pub fn texture_id(&self) -> TextureId {
        self.texture_id
    }

    pub fn position(&self) -> Coord {
        self.object.pos
    }

    pub fn width(&self) -> i32 {
        self.object.width()
    }

    pub fn height(&self) -> i32 {
        self.object.height()
    }

    pub fn game_object(&self) -> &GameObject {
        &self.object
    }

    pub fn stage(&self) -> Stage {
        self.trajectory.stage()
    }

    /// Angle() twin: hull attitude plus the spin-animation offset.
    pub fn angle(&self) -> f32 {
        self.angle + self.spin_angle
    }

    pub fn scale(&self) -> f32 {
        self.object.scale
    }

    /// CannonPosition twin: muzzle just below the hull (ceil half-height).
    pub fn cannon_position(&self) -> Coord {
        let mut pos = self.object.pos;
        pos.y += (self.height() + 1) / 2;
        pos
    }

    /// Re-points the trajectory at the fleet's current cruise base. The C++
    /// twin needs no such call: `Trajectory` stores a `const Coord*` into the
    /// manager's `base_cruise_`, so every read sees the live value. The Rust
    /// kernel avoids shared ownership, so the fleet pushes the base into every
    /// trajectory once per tick -- right after the drift update and before any
    /// alien moves -- reproducing the pointer's read-at-use semantics exactly.
    pub fn set_base_cruise(&mut self, base_cruise: (i32, i32)) {
        self.trajectory.set_base_cruise(base_cruise);
    }

    pub fn trajectory(&self) -> &Trajectory {
        &self.trajectory
    }

    pub fn build_attack(&mut self) {
        let from = self.object.pos;
        self.trajectory.build_attack((from.x, from.y));
    }

    pub fn build_kamikaze_dive(&mut self, target_player: Coord) {
        let from = self.object.pos;
        self.trajectory
            .build_kamikaze_dive((from.x, from.y), (target_player.x, target_player.y));
    }

    /// ForceCruise twin: teleport-dock at the formation station, consuming
    /// the arrival path and any in-flight attack spline.
    pub fn force_cruise(&mut self, target: Coord) {
        let v = target.to_vec2f();
        self.transform_pos = v;
        self.transform_prev = v;
        self.object.pos = target;
        self.trajectory.force_cruise();
    }

    pub fn set_attack_timer(&mut self, frames: i32) {
        self.attack_wait_timer = frames;
    }

    pub fn decrement_attack_timer(&mut self) {
        if self.attack_wait_timer > 0 {
            self.attack_wait_timer -= 1;
        }
    }

    pub fn should_launch_attack(&self) -> bool {
        self.trajectory.stage() == Stage::Cruising && self.attack_wait_timer == 0
    }

    pub fn set_kamikaze(&mut self, k: bool) {
        self.is_kamikaze = k;
    }

    pub fn is_kamikaze(&self) -> bool {
        self.is_kamikaze
    }

    pub fn set_wanderer(&mut self, w: bool) {
        self.is_wanderer = w;
    }

    pub fn is_wanderer(&self) -> bool {
        self.is_wanderer
    }

    pub fn can_trigger_alpha_decay(&self) -> bool {
        self.texture_id == TextureId::Alien4 && self.has_electrosphere && self.health > 1
    }

    /// TriggerAlphaDecay twin: the first hit sheds the electrosphere mass
    /// (scale 0.5, faster orbit, 1.3x hull speed); only the second can kill.
    pub fn trigger_alpha_decay(&mut self) {
        if self.health <= 1 || !self.has_electrosphere {
            return;
        }
        self.health -= 1;
        self.is_alpha_decayed = true;
        self.object.scale = ALIEN4_ALPHA_DECAY_SCALE;
    }

    pub fn can_trigger_warp(&self) -> bool {
        self.has_warp_property && self.warp_evasions_remaining > 0 && !self.is_warping
    }

    pub fn warp_evasions_remaining(&self) -> i32 {
        self.warp_evasions_remaining
    }

    pub fn is_warping(&self) -> bool {
        self.is_warping
    }

    /// GetGreatestRadius twin: circumradius of the (scaled) hull rectangle.
    pub fn get_greatest_radius(&self) -> f32 {
        let hw = self.width() as f32 * 0.5;
        let hh = self.height() as f32 * 0.5;
        hw.hypot(hh)
    }

    /// ExecuteWarp twin: consume one evasion, enter transit toward the cached
    /// destination, permanently relocate the honeycomb station, abort any
    /// dive and re-arm a long attack wait (draw order preserved).
    pub fn execute_warp(&mut self, decision: formation::WarpDecision, sink: &mut EventSink) {
        self.warp_evasions_remaining -= 1;
        self.is_warping = true;
        self.warp_frames_left = ALIEN14_WARP_MAX_FRAMES;
        self.warp_cooldown_timer =
            fast_round_f32(ALIEN14_WARP_COOLDOWN_SECONDS * SIMULATION_FREQUENCY_HZ);

        self.trajectory.relocate_station(decision.col, decision.row);
        self.trajectory.force_cruise();
        let max_wait = self.trajectory.get_max_attack_wait_frames();
        self.attack_wait_timer = self.trajectory.uniform_int(
            ALIEN14_POST_WARP_ATTACK_WAIT_MIN_FRAMES,
            ALIEN14_POST_WARP_ATTACK_WAIT_MIN_FRAMES.max(max_wait),
        );

        self.warp_dest = Coord::new(decision.target_x, decision.target_y);

        sink.floating_text(
            self.position(),
            "RELATIVISTIC WARP EVASION!",
            0,
            240,
            255,
            FLOATING_TEXT_NOTICE_FONT_SIZE,
            65,
        );
        sink.flash(0, 200, 255, 6);
    }

    /// TriggerSpacetimeWarp twin: the only hard gate is the jump budget; the
    /// restricted draw runs over the ACTIVE formation rows (the current
    /// wave's convoy rows, never the screen capacity).
    pub fn trigger_spacetime_warp(
        &mut self,
        occupancy: &[u16; MAX_GRID_ROWS],
        player_x: i32,
        player_y: i32,
        formation_rows: i32,
        sink: &mut EventSink,
    ) {
        if self.warp_evasions_remaining <= 0 {
            return;
        }
        let origin_col = self.trajectory.grid_col();
        let origin_row = self.trajectory.grid_row();
        let used_cols = self.trajectory.used_cols().clamp(1, 16);
        let used_rows = formation_rows.clamp(1, MAX_GRID_ROWS as i32);

        let metrics = self.trajectory.metrics();
        // The grid snapshot is cloned out so the draw below can hold the
        // trajectory's RNG mutably in the same call (warps are rare; the C++
        // twin reads the shared global instead).
        let grid = self.trajectory.grid().clone();
        let base_cruise = self.trajectory.base_cruise();
        let stage_cycle = self.trajectory.stage_cycle();
        let decision = formation::compute_relativistic_warp(
            &grid,
            origin_col,
            origin_row,
            used_cols,
            used_rows,
            player_x,
            player_y,
            base_cruise,
            stage_cycle,
            metrics.scale,
            metrics.win_w,
            metrics.win_h,
            occupancy,
            self.trajectory.rng_mut(),
        );
        self.execute_warp(decision, sink);
    }

    /// Twin of Alien::Move (the fixed 60 Hz physics tick).
    pub fn step(&mut self) {
        self.transform_prev = self.transform_pos;
        // The transit frame includes its final landing step even though
        // is_warping clears mid-tick: remember the pre-tick flag for the
        // Delta Jump Guard below.
        let warp_transit_this_frame = self.is_warping;

        let s = self.trajectory.metrics().scale;
        let move_speed_mult = if self.is_alpha_decayed {
            ALIEN4_DECAYED_SPEED_MULT
        } else {
            1.0
        };
        let scaled_vel =
            (self.speed * s * move_speed_mult).max(FLEET_MIN_VELOCITY_PIXELS);

        if self.warp_cooldown_timer > 0 {
            self.warp_cooldown_timer -= 1;
        }

        // Both branches below assign before the guard reads them (the C++
        // initializes from the current position and overwrites unconditionally).
        let mut target_x;
        let mut target_y;
        // The classic post-dive wrap-around (attacking -> joining) is the one
        // intentional off-screen teleport and must bypass the jump guard.
        let mut dive_wraparound = false;
        if self.is_warping {
            let warp_step = ALIEN14_WARP_SPEED_PIXELS * s;
            // Priority 4b: consume strictly the cached destination; never
            // re-sample per frame (prevents jitter).
            let dest = self.warp_dest;
            let diff_x = dest.x as f32 - self.transform_pos.x;
            let diff_y = dest.y as f32 - self.transform_pos.y;
            let diff_dist = (diff_x * diff_x + diff_y * diff_y).sqrt();

            let arrived = diff_dist <= warp_step;
            if self.warp_frames_left > 0 {
                self.warp_frames_left -= 1;
            }
            if arrived || self.warp_frames_left <= 0 {
                self.transform_pos.x = dest.x as f32;
                self.transform_pos.y = dest.y as f32;
                self.is_warping = false;
                self.warp_frames_left = 0;
            } else {
                self.transform_pos =
                    advance_toward(self.transform_pos, dest.to_vec2f(), warp_step);
            }
            target_x = self.transform_pos.x;
            target_y = self.transform_pos.y;
        } else {
            let prev_stage = self.trajectory.stage();
            let (nx, ny) = self.trajectory.next_position_f(
                self.transform_pos.x,
                self.transform_pos.y,
                scaled_vel,
            );
            target_x = nx;
            target_y = ny;
            dive_wraparound =
                prev_stage == Stage::Attacking && self.trajectory.stage() == Stage::Joining;
            if self.trajectory.stage() == Stage::Cruising
                && prev_stage != Stage::Cruising
                && self.attack_wait_timer <= 0
            {
                let max_wait = self.trajectory.get_max_attack_wait_frames();
                self.attack_wait_timer =
                    self.trajectory.uniform_int(MIN_ATTACK_WAIT_FRAMES, max_wait);
            }
        }

        // Delta Jump Guard: outside warp transit, no tick may move further
        // than scaled_vel per axis. Warp-capable hulls get a direction-
        // preserving clamp; everyone else gets the baseline reset (the
        // teleport is intentional, the guard must not fight it).
        let jump_x = target_x - self.transform_prev.x;
        let jump_y = target_y - self.transform_prev.y;
        let max_axis = jump_x.abs().max(jump_y.abs());
        if !self.is_warping && !warp_transit_this_frame && max_axis > scaled_vel {
            if self.has_warp_property && !dive_wraparound {
                let clamped = clamp_step_to_budget(
                    self.transform_prev,
                    Vec2f::new(target_x, target_y),
                    scaled_vel,
                );
                target_x = clamped.x;
                target_y = clamped.y;
            } else {
                self.transform_prev = Vec2f::new(target_x, target_y);
            }
        }

        self.transform_pos = Vec2f::new(target_x, target_y);
        self.object.pos = Coord::round(self.transform_pos.x, self.transform_pos.y);

        let dx = target_x - self.transform_prev.x;
        let dy = target_y - self.transform_prev.y;
        let move_dist = (dx * dx + dy * dy).sqrt();

        if move_dist > 1e-3 {
            // Desired hull attitude: upright while docked (the 1 px/frame
            // formation drift must never tilt the ship), otherwise the actual
            // travel direction of this tick.
            let target_angle = if self.trajectory.stage() == Stage::Cruising {
                0.0
            } else {
                dy.atan2(dx) * (180.0 / std::f32::consts::PI) - 90.0
            };
            self.angle = advance_turn_deg(
                self.angle,
                target_angle,
                TURN_RESPONSE_FACTOR,
                TURN_SNAP_THRESHOLD_DEG,
                MAX_TURN_RATE_DEG,
            );
        }

        if self.spin_timer > 0 {
            self.spin_timer -= 1;
            self.spin_angle = spin_angle_table()[self.spin_timer.max(0) as usize];
        } else if !self.has_spun && self.trajectory.stage() == Stage::Attacking {
            if self.trajectory.chance(1, ALIEN_SPIN_CHANCE_DENOMINATOR) {
                self.spin_timer = ALIEN_SPIN_ANIMATION_FRAMES;
                self.has_spun = true;
            }
        }

        // Deterministic animation evolution on the fixed 60 Hz step.
        if self.is_kamikaze {
            self.kamikaze_phase += KAMIKAZE_PULSE_PHASE_STEP;
        }
        if self.texture_id == TextureId::Alien4 && self.has_electrosphere {
            let speed_mult = if self.is_alpha_decayed {
                ALIEN4_DECAYED_ORBIT_SPEED_MULT
            } else {
                1.0
            };
            self.electron_phase += ALIEN4_ELECTRON_PHASE_STEP * speed_mult;
        }
    }
}
