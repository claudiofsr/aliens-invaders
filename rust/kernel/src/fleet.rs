//! Port of AliensManager (src/entities/managers.{h,cc}): convoy spawning with
//! simultaneous-group lockstep, the cruise drift sweep, wanderer flank
//! spawns, the attack-timer economy, kamikaze quota shuffle, the alien fire
//! director (seekers, trap gates, relativistic/radioactive wave flags), the
//! bullet-collision resolution chain and the bonus economy.
//!
//! Scope notes (RUST.md slice 4b):
//! - The C++ twin holds manager pointers as const members; the Rust twin
//!   takes them per call so the harness destructures its world (no shared
//!   ownership, borrow-checker enforced tick order).
//! - The recycle bin is omitted (Respawn == fresh construction, see alien.rs).
//!   `original_max_convoy_size_` -- which the C++ rebuild path also writes but
//!   only the resize cascade reads -- became a real field in Fatia 13, when
//!   the corpus started exercising `on_resize` (ent.rz.fleet rows).
//! - Score rides per call (`Option<&mut Score>` on the collision entry):
//!   the production `AliensManager` holds a `Score*` member, wired live in
//!   the game and null in the Fatia-4 fleet corpus. Fatia 11 pins the wired
//!   economy (`ent.fb.*`); with a null/None score every gate opens, which is
//!   bit-identical to the recorded nullptr behavior.
//! - RNG discipline: `dist::uniform_int` twins the std::uniform_int_distribution
//!   call sites; `rng.uniform_int` twins the native RandomStream::UniformInt
//!   sites. Mixing them up shifts every later draw.

use crate::alien::Alien;
use crate::bonus::{BonusManager, BonusType};
use crate::bullets::BulletsManager;
use crate::dist;
use crate::explosions::ExplosionsManager;
use crate::formation::{self, GridData, MAX_GRID_COLS, MAX_GRID_ROWS};
use crate::game_object::GameObject;
use crate::mathx::{fast_round_f32, fast_round_f64, Coord};
use crate::path::{generate_smooth_spline, Vec2f};
use crate::player::Player;
use crate::random_stream::RandomStream;
use crate::score::{Score, MAX_BONUSES_PER_STAGE};
use crate::sink::EventSink;
use crate::stage_catalog::{self, StageProfile};
use crate::textures::TextureId;
use crate::trajectory::{Stage, Trajectory, ViewportMetrics};

// GameRules constants (golden-vector policed copies of src/core/constants.h).
const MAX_ALIENS_COUNT: usize = 256;
const RANDOM_WANDERERS_COUNT: i32 = 4;
const POST_FANFARE_ARMADA_DELAY_FRAMES: i32 = 60;
const FLEET_CRUISE_DRIFT_PIXELS: f32 = 1.0;
const FLEET_MARGIN_X_PIXELS: i32 = 14;
const MIN_SPAWN_WAIT_FRAMES: i32 = 5;
const CONVOY_SPAWN_GAP_FACTOR: f32 = 1.10;
const CONVOY_GAP_SAME_LANE_FACTOR: f32 = 1.75;
const CONVOY_GAP_OTHER_LANE_FACTOR: f32 = 1.18;
const MIN_ATTACK_WAIT_FRAMES: i32 = 30;
const WANDERER_MAX_WAIT_FRAMES: i32 = 180;
const ALIEN_TEXTURE_COUNT: i32 = 15;

const FIRE_CHANCE_MIN: i32 = 20;
const FIRE_CHANCE_FLEET_FACTOR: i32 = 50;
const FIRE_CHANCE_FLEET_DIVISOR: i32 = 25;
const ALIEN_FIRE_COOLDOWN: i32 = 8;
const SEEKER_MISSILE_PROBABILITY: f32 = 0.30;
const MAX_DEFLECTION_ANGLE_DEG: f32 = 20.0;
const DEFLECTION_JITTER_DEG: i32 = 2;
const SAFE_EVASION_CORRIDOR_PIXELS: f32 = 118.0;
const BOMB_NEAR_DISTANCE_PIXELS: f32 = 68.0;
const BOMB_SPEED_BASE_MULTIPLIER: f32 = 0.70;
const BOMB_SPEED_BASE_OFFSET: f32 = 1.8;
const BOMB_MIN_SPEED: f32 = 2.8;
const BOMB_MAX_HORIZONTAL_SPEED_RATIO: f32 = 0.32;
const BOMB_HORIZONTAL_SPEED_MULTIPLIER: f32 = 0.40;
const BOMB_SPEED_INDIVIDUAL_VARIANCE_BASE: f32 = 0.84;
const BOMB_SPEED_INDIVIDUAL_VARIANCE_RANGE: i32 = 34;
const BOMB_SPEED_VECTOR_VARIANCE_BASE: f32 = 0.88;
const BOMB_SPEED_VECTOR_VARIANCE_RANGE: i32 = 29;
const TURNING_SPACING_COOLDOWN_MIN_FRAMES: i32 = 35;
const TURNING_SPACING_COOLDOWN_MAX_FRAMES: i32 = 79;
const TURNING_TRIGGER_Y_MIN_PERCENT: i32 = 38;
const TURNING_TRIGGER_Y_MAX_PERCENT: i32 = 69;

const BONUS_INITIAL_WAIT_MIN_FRAMES: i32 = 5;
const BONUS_INITIAL_WAIT_MAX_FRAMES: i32 = 10;
const BONUS_WAVE_DROP_PROBABILITY: f32 = 0.50;
const BONUS_WEIGHT_FIRE: i32 = 25;
const BONUS_WEIGHT_MULTI: i32 = 25;
const BONUS_WEIGHT_SPEED: i32 = 25;
const BONUS_WEIGHT_SHIELD: i32 = 15;
const BONUS_WEIGHT_NUKE: i32 = 10;

const SCORE_PER_ALIEN_HIT: u64 = 10;
const SCORE_PER_KAMIKAZE_ALIEN_HIT: u64 = 50;
const SCORE_PER_ALIEN_NUKED: u64 = 20;
const COLLISION_HITBOX_SCALE: f32 = 0.65;
const COLLISION_BAND_MARGIN_PIXELS: i32 = 40;
const ALIEN14_WARP_TRIGGER_RADIUS_FACTOR: f32 = 2.4;

const FLOATING_TEXT_HIT_FONT_SIZE: f32 = 24.0;
const FLOATING_TEXT_NUKE_HIT_FONT_SIZE: f32 = 25.0;

// SoundManager sfx ids (see textures.rs::sfx).
const SFX_POP_LIGHT: i32 = 1;
const SFX_POP_MEDIUM: i32 = 2;
const SFX_POP_HEAVY: i32 = 3;
const SFX_KAMIKAZE_ALERT: i32 = 4;
const SFX_KAMIKAZE_EXPLODE: i32 = 5;
const SFX_POWERUP_SPEED: i32 = 10;

// Engine palettes (kSeekerEngineColors / special-entity tints).
const SEEKER_ENGINE_COLORS: [(u8, u8, u8); 4] =
    [(210, 60, 255), (255, 180, 25), (0, 230, 255), (50, 255, 100)];
const ALIEN14_ENGINE_COLOR: (u8, u8, u8) = (0, 220, 255);
const ALIEN4_RADIO_ENGINE_COLOR: (u8, u8, u8) = (50, 255, 100);

/// Twin of ConvoyData: one wave's spawn recipe. `path_id` replaces the C++
/// FlightPath POINTER identity used by the same-lane gap heuristic.
#[derive(Clone, Copy)]
pub struct ConvoySpec {
    pub wait: i32,
    pub texture_id: TextureId,
    pub convoy_size_pc: i32,
    pub x_mirror: bool,
    pub split: bool,
    pub simultaneous: bool,
    /// 0 = standard center, 1 = outer left wing, 2 = outer right wing.
    pub dock_align: i32,
    pub path_id: usize,
}

/// ConvoyData::ConvoySize twin: aspect-scaled percentage of the honeycomb
/// columns, contained to the fixed super-table width.
pub fn convoy_size(spec: &ConvoySpec, window_w: i32, spacing_x: i32) -> i32 {
    let ratio = window_w as f64 / spacing_x as f64;
    fast_round_f64(ratio * spec.convoy_size_pc as f64 / 100.0)
        .clamp(1, MAX_GRID_COLS as i32)
}

/// Twin of BulletCollisionSummary.
#[derive(Default, Clone, Copy)]
pub struct CollisionSummary {
    pub total_hits: i32,
    pub regular_hits: i32,
    pub kamikaze_hits: i32,
    pub score_earned: u64,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum FleetState {
    SpawningConvoys,
    ActiveCombat,
}

/// AliensManager twin.
pub struct Fleet {
    level_number: i32,
    stage_profile: StageProfile,
    rng: RandomStream,
    convoys: Vec<ConvoySpec>,
    /// Arrival splines by path_id (the corpus re-emits them as rows).
    paths: Vec<Vec<Vec2f>>,
    nconvoys: i32,
    max_convoy_size: i32,
    /// Un-capped convoy metric snapshot: the resize cascade recomputes it
    /// from the fresh viewport, then derives the capped `max_convoy_size`
    /// from it (the C++ `original_max_convoy_size_`).
    original_max_convoy_size: i32,
    convoy_idx: i32,
    convoy_alien_idx: i32,
    speed: f32,
    base_cruise: Coord,
    base_cruise_speed: i32,
    metrics: ViewportMetrics,
    /// Snapshot of the formation globals (fixed by the corpus-initial
    /// Recompute(scale); Trajectory keeps its own equivalent snapshot).
    grid: GridData,
    fleet_state: FleetState,
    next_creation_wait: i32,
    fire_cooldown: i32,
    turning_spacing_cooldown: i32,
    bonus_wait: i32,
    bonus_allowed_this_level: bool,
    bonus_spawned_this_level: bool,
    random_wanderers_spawned: bool,
    wanderers_allowed_cycle: bool,
    min_cruise_x: i32,
    max_cruise_x: i32,
    aliens: Vec<Alien>,
}

impl Fleet {
    /// Twin of the AliensManager constructor + Rebuild. The trailing RNG draw
    /// order is part of the wave contract: bonus_wait first (std
    /// distribution), then the Bernoulli drop gate (native stream).
    pub fn new(
        level_number: i32,
        convoys: Vec<ConvoySpec>,
        paths: Vec<Vec<Vec2f>>,
        metrics: ViewportMetrics,
        max_level: i32,
        random_seed: u32,
    ) -> Self {
        assert!(
            level_number >= 1
                && !convoys.is_empty()
                && convoys[0].texture_id != TextureId::None,
            "AliensManager initialization error"
        );
        let stage_profile = stage_catalog::get_profile(level_number, max_level);
        let grid = formation::recompute(metrics.scale);
        let nconvoys = stage_profile.convoy_count;
        let speed = stage_profile.fleet_speed;
        let wanderers_allowed_cycle = stage_profile.wanderers_allowed;

        let mut fleet = Self {
            level_number,
            stage_profile,
            rng: RandomStream::new(random_seed),
            convoys,
            paths,
            nconvoys,
            max_convoy_size: 0,
            original_max_convoy_size: 0,
            convoy_idx: 0,
            convoy_alien_idx: 0,
            speed,
            base_cruise: Coord::new(0, 0),
            base_cruise_speed: 1,
            metrics,
            grid,
            fleet_state: FleetState::SpawningConvoys,
            next_creation_wait: POST_FANFARE_ARMADA_DELAY_FRAMES,
            fire_cooldown: 0,
            turning_spacing_cooldown: 0,
            bonus_wait: 0,
            bonus_allowed_this_level: false,
            bonus_spawned_this_level: false,
            random_wanderers_spawned: false,
            wanderers_allowed_cycle,
            min_cruise_x: 0,
            max_cruise_x: 0,
            aliens: Vec::with_capacity(MAX_ALIENS_COUNT),
        };

        fleet.max_convoy_size = fleet.get_max_convoy_size();
        // The C++ ctor/Rebuild pair keeps the un-capped metric alongside the
        // capped one (original_max_convoy_size_ = max_convoy_size_ BEFORE the
        // wanderer inflation); the resize cascade reads it back.
        fleet.original_max_convoy_size = fleet.max_convoy_size;
        if fleet.wanderers_allowed_cycle {
            // The four wanderer flank columns must also fit the fixed
            // super-table width: one clamp keeps every downstream col_idx a
            // legal honeycomb station at any window aspect.
            fleet.max_convoy_size =
                (fleet.max_convoy_size + RANDOM_WANDERERS_COUNT).min(MAX_GRID_COLS as i32);
        }

        fleet.bonus_wait = dist::uniform_int(
            &mut fleet.rng,
            BONUS_INITIAL_WAIT_MIN_FRAMES,
            BONUS_INITIAL_WAIT_MAX_FRAMES,
        );
        fleet.bonus_allowed_this_level =
            fleet.rng.bernoulli(BONUS_WAVE_DROP_PROBABILITY);

        fleet.update_cruise_bounds();
        fleet.base_cruise.x = (fleet.min_cruise_x + fleet.max_cruise_x) / 2;
        fleet.base_cruise.y = fleet.grid.base_cruise_y;
        fleet
    }

    fn get_max_convoy_size(&self) -> i32 {
        let mut res = 0;
        for c in self.convoys.iter().take(self.nconvoys as usize) {
            let size = convoy_size(c, self.metrics.win_w, self.grid.spacing_x);
            if size > res {
                res = size;
            }
        }
        res
    }

    fn update_cruise_bounds(&mut self) {
        let new_w = self.metrics.win_w;
        let half_w = self.grid.width / 2;
        let s = self.metrics.scale;
        let margin_x = half_w + fast_round_f32(FLEET_MARGIN_X_PIXELS as f32 * s);
        self.min_cruise_x = margin_x;
        let fleet_span = (self.max_convoy_size - 1) * self.grid.spacing_x
            + self.grid.spacing_x / 2;
        self.max_cruise_x =
            (self.min_cruise_x + 1).max(new_w - margin_x - fleet_span);
    }

    /// BuildOccupancyBitset twin. `exclude` is the index of the alien the
    /// query is about (it may of course not block its own warp landing);
    /// stations of convoys that have NOT spawned yet count as occupied.
    fn build_occupancy_bitset(&self, exclude: Option<usize>) -> [u16; MAX_GRID_ROWS] {
        let mut occ = [0u16; MAX_GRID_ROWS];
        for (i, alien) in self.aliens.iter().enumerate() {
            if Some(i) == exclude {
                continue;
            }
            let c = alien.trajectory().grid_col();
            let r = alien.trajectory().grid_row();
            if r >= 0
                && r < MAX_GRID_ROWS as i32
                && c >= 0
                && c < MAX_GRID_COLS as i32
            {
                occ[r as usize] |= 1u16 << c;
            }
        }
        let mut c = self.convoy_idx;
        while c < self.nconvoys {
            let row = self.nconvoys - c - 1;
            if row >= 0 && row < MAX_GRID_ROWS as i32 {
                let spec = &self.convoys[c as usize];
                let size = convoy_size(spec, self.metrics.win_w, self.grid.spacing_x);
                let mut first_col = (self.max_convoy_size - size) / 2;
                if spec.dock_align == 1 {
                    first_col = 0;
                } else if spec.dock_align == 2 {
                    first_col = (self.max_convoy_size - size).max(0);
                }
                let mut k = 0;
                while k < size {
                    let col = first_col + k;
                    if col >= 0 && col < MAX_GRID_COLS as i32 {
                        occ[row as usize] |= 1u16 << col;
                    }
                    k += 1;
                }
            }
            c += 1;
        }
        occ
    }

    /// SpawnRandomWanderers twin: four flank columns on the SHARED row 0,
    /// repair-walked around whatever already docks there.
    fn spawn_random_wanderers(&mut self) {
        if !self.wanderers_allowed_cycle || self.random_wanderers_spawned {
            return;
        }
        self.random_wanderers_spawned = true;

        let top_row = 0;
        let stage_cycle = stage_catalog::wave_to_stage(self.level_number);
        let cols = self.max_convoy_size.clamp(1, MAX_GRID_COLS as i32);
        let mut taken = self.build_occupancy_bitset(None)[0];

        for i in 0..RANDOM_WANDERERS_COUNT {
            let mut col_idx = if i == 0 {
                0
            } else if i == 1 {
                1
            } else if i == 2 {
                cols - 2
            } else {
                cols - 1
            };
            col_idx = col_idx.clamp(0, cols - 1);
            if taken & (1u16 << col_idx) != 0 {
                let dir = if col_idx * 2 < cols { 1 } else { -1 };
                let mut free_col = -1;
                let mut c = col_idx + dir;
                while c >= 0 && c < cols {
                    if taken & (1u16 << c) == 0 {
                        free_col = c;
                        break;
                    }
                    c += dir;
                }
                if free_col < 0 {
                    let mut c = 0;
                    while c < cols {
                        if taken & (1u16 << c) == 0 {
                            free_col = c;
                            break;
                        }
                        c += 1;
                    }
                }
                if free_col >= 0 {
                    col_idx = free_col; // saturated row: keep the preference
                }
            }
            taken |= 1u16 << col_idx;

            // Same honeycomb cell the rest of the armada uses -- never a raw
            // col*SpacingX() product (odd rows would lose the stagger).
            let slot = formation::get_slot(&self.grid, stage_cycle, top_row, col_idx);
            let target = Coord::new(
                self.base_cruise.x + slot.offset_x,
                self.base_cruise.y + slot.offset_y,
            );

            let win_w = self.metrics.win_w.max(1) as f32;
            let win_h = self.metrics.win_h.max(1) as f32;
            let pt = Vec2f::new(
                (target.x as f32 * 1024.0) / win_w,
                (target.y as f32 * 1024.0) / win_h,
            );

            // Two-pass resample of the degenerate {pt, pt} control pair: the
            // exact spline pipeline the C++ writes into reused buffers.
            let raw_pts = generate_smooth_spline(&[pt, pt]);
            let path = generate_smooth_spline(&raw_pts);

            let rnd_sprite = crate::textures::alien_sprite(dist::uniform_int(
                &mut self.rng,
                0,
                ALIEN_TEXTURE_COUNT - 1,
            ));

            let seed = self.rng.next_u32();
            let trajectory = Trajectory::new(
                &path,
                false,
                (self.base_cruise.x, self.base_cruise.y),
                self.metrics,
                col_idx,
                top_row,
                seed,
                stage_cycle,
                self.max_convoy_size,
            );
            let mut alien = Alien::new(trajectory, self.speed, rnd_sprite);
            alien.set_wanderer(true);
            alien.force_cruise(target);
            let wait = dist::uniform_int(&mut self.rng, 0, WANDERER_MAX_WAIT_FRAMES);
            alien.set_attack_timer(wait);
            if self.aliens.len() < MAX_ALIENS_COUNT {
                self.aliens.push(alien);
            }
            // Over-capacity releases are unobservable (the recycled alien
            // respawns identical) and the draws above stay unconditional.
        }
    }

    /// AliensManager::Move twin: formation drift, per-alien warp trigger and
    /// physics, then the spawn/attack director.
    pub fn advance(
        &mut self,
        bullets: &BulletsManager,
        sink: &mut EventSink,
        player_pos: Option<Coord>,
    ) {
        // Formation drift: one design pixel per frame at scale 1.0, reversed
        // at the cruise bounds -- the classic armada sweep.
        let scaled_cruise_spd =
            1.max(fast_round_f32(FLEET_CRUISE_DRIFT_PIXELS * self.metrics.scale));
        self.base_cruise.x += if self.base_cruise_speed >= 0 {
            scaled_cruise_spd
        } else {
            -scaled_cruise_spd
        };

        if self.base_cruise.x <= self.min_cruise_x {
            self.base_cruise.x = self.min_cruise_x;
            self.base_cruise_speed = self.base_cruise_speed.abs();
        } else if self.base_cruise.x >= self.max_cruise_x {
            self.base_cruise.x = self.max_cruise_x;
            self.base_cruise_speed = -self.base_cruise_speed.abs();
        }

        // Live-pointer parity: docked cruise targets AND warp destinations read
        // the base at use time in C++ (`Trajectory::base_cruise_` aliases the
        // manager field). Syncing here -- after this frame's drift/bounce and
        // before the alien loop -- makes every later read in this tick see the
        // same value the C++ pointer would.
        let bc = (self.base_cruise.x, self.base_cruise.y);
        for alien in &mut self.aliens {
            alien.set_base_cruise(bc);
        }

        let mut i = 0;
        while i < self.aliens.len() {
            // Spec trigger ("Passo 1"): a bullet penetrating the proximity
            // radius of a relativistic Alien14 with evasions left fires the
            // jump BEFORE contact.
            let warping_candidate = self.aliens[i].texture_id() == TextureId::Alien14
                && self.aliens[i].can_trigger_warp();
            if warping_candidate {
                let radius = self.aliens[i].get_greatest_radius();
                let trigger_dist = radius * ALIEN14_WARP_TRIGGER_RADIUS_FACTOR;
                let pos = self.aliens[i].position();
                if bullets.has_projectile_within_radius(pos, trigger_dist) {
                    let occ = self.build_occupancy_bitset(Some(i));
                    let px = player_pos.map_or(0, |p| p.x);
                    let py = player_pos.map_or(0, |p| p.y);
                    self.aliens[i].trigger_spacetime_warp(&occ, px, py, self.nconvoys, sink);
                    sink.sfx(SFX_POWERUP_SPEED, 0.0);
                }
            }
            self.aliens[i].step();
            i += 1;
        }

        self.creation(sink, player_pos);
    }

    /// CheckKamikazeProximity twin: any guided diver inside the lethal blast
    /// radius of the starship this frame.
    pub fn check_kamikaze_proximity(&self, player_pos: Coord, lethal_radius: f32) -> bool {
        let lethal_sq = lethal_radius * lethal_radius;
        for alien in &self.aliens {
            if alien.is_kamikaze() && alien.stage() == Stage::Attacking {
                let dx = (alien.position().x - player_pos.x) as f32;
                let dy = (alien.position().y - player_pos.y) as f32;
                if dx * dx + dy * dy <= lethal_sq {
                    return true;
                }
            }
        }
        false
    }

    /// Creation twin: wanderer gate, simultaneous-group convoy lockstep and
    /// the combat-phase attack director.
    fn creation(&mut self, sink: &mut EventSink, player_pos: Option<Coord>) {
        if self.wanderers_allowed_cycle && !self.random_wanderers_spawned {
            let halfway = (self.nconvoys + 1) / 2;
            if self.convoy_idx >= halfway {
                self.spawn_random_wanderers();
            }
        }

        match self.fleet_state {
            FleetState::SpawningConvoys => {
                if self.next_creation_wait > 0 {
                    self.next_creation_wait -= 1;
                    return;
                }

                if self.convoy_idx < self.nconvoys {
                    // Group consecutive simultaneous convoys to spawn their
                    // alien ranks in lockstep.
                    let mut group_end = self.convoy_idx + 1;
                    while group_end < self.nconvoys
                        && self.convoys[group_end as usize].simultaneous
                    {
                        group_end += 1;
                    }

                    let s = self.metrics.scale;
                    let scaled_vel = 2.max(fast_round_f32(self.speed * s));
                    let stage_cycle = stage_catalog::wave_to_stage(self.level_number);

                    let mut c = self.convoy_idx;
                    while c < group_end {
                        let spec = self.convoys[c as usize];
                        let size =
                            convoy_size(&spec, self.metrics.win_w, self.grid.spacing_x);
                        if self.convoy_alien_idx < size
                            && self.aliens.len() < MAX_ALIENS_COUNT
                        {
                            // Flank-docking: left incursions dock on the left
                            // wing, right incursions on the right wing.
                            let col_idx = if spec.dock_align == 1 {
                                self.convoy_alien_idx
                            } else if spec.dock_align == 2 {
                                (self.max_convoy_size - size).max(0) + self.convoy_alien_idx
                            } else {
                                self.convoy_alien_idx + (self.max_convoy_size - size) / 2
                            };

                            let row_idx = self.nconvoys - c - 1;
                            let mirrored = spec.x_mirror
                                ^ (spec.split && self.convoy_alien_idx % 2 == 0);

                            let seed = self.rng.next_u32();
                            let path = self.paths[spec.path_id].clone();
                            let bc = (self.base_cruise.x, self.base_cruise.y);
                            let metrics = self.metrics;
                            let mcs = self.max_convoy_size;
                            let trajectory = Trajectory::new(
                                &path, mirrored, bc, metrics, col_idx, row_idx,
                                seed, stage_cycle, mcs,
                            );
                            let alien = Alien::new(trajectory, self.speed, spec.texture_id);
                            self.aliens.push(alien);
                        }
                        c += 1;
                    }

                    let mut all_done = true;
                    let mut c = self.convoy_idx;
                    while c < group_end {
                        let size = convoy_size(
                            &self.convoys[c as usize],
                            self.metrics.win_w,
                            self.grid.spacing_x,
                        );
                        if self.convoy_alien_idx + 1 < size {
                            all_done = false;
                            break;
                        }
                        c += 1;
                    }

                    if !all_done {
                        self.convoy_alien_idx += 1;
                        let spawn_dist = fast_round_f32(
                            CONVOY_SPAWN_GAP_FACTOR * self.grid.height as f32,
                        );
                        self.next_creation_wait =
                            MIN_SPAWN_WAIT_FRAMES.max(spawn_dist / scaled_vel);
                    } else {
                        self.convoy_alien_idx = 0;
                        self.convoy_idx = group_end;
                        if self.convoy_idx < self.nconvoys {
                            let cur = self.convoys[self.convoy_idx as usize];
                            let prev = self.convoys[self.convoy_idx as usize - 1];
                            let same_lane =
                                cur.path_id == prev.path_id && cur.x_mirror == prev.x_mirror;
                            let factor = if same_lane {
                                CONVOY_GAP_SAME_LANE_FACTOR
                            } else {
                                CONVOY_GAP_OTHER_LANE_FACTOR
                            };
                            let min_gap_frames = fast_round_f32(
                                factor * self.grid.height as f32,
                            ) / scaled_vel;
                            self.next_creation_wait =
                                min_gap_frames.max(self.convoys[self.convoy_idx as usize].wait);
                        }
                    }
                } else {
                    // Phase transition: arm every docked alien's attack wait
                    // (draw order = fleet order), then deal the kamikaze
                    // quota over a std::shuffle of the index range.
                    self.fleet_state = FleetState::ActiveCombat;
                    let max_wait =
                        stage_catalog::get_max_attack_wait_frames(self.level_number);
                    for alien in self.aliens.iter_mut() {
                        let wait = if alien.is_wanderer() {
                            dist::uniform_int(
                                &mut self.rng,
                                MIN_ATTACK_WAIT_FRAMES,
                                WANDERER_MAX_WAIT_FRAMES,
                            )
                        } else {
                            dist::uniform_int(&mut self.rng, MIN_ATTACK_WAIT_FRAMES, max_wait)
                        };
                        alien.set_attack_timer(wait);
                    }

                    let kamikaze_quota =
                        stage_catalog::get_kamikaze_quota(self.level_number);
                    let mut assigned = 0;
                    let count = self.aliens.len().min(MAX_ALIENS_COUNT);
                    let indices = dist::shuffle_indices(&mut self.rng, count);
                    for idx in indices {
                        if assigned >= kamikaze_quota {
                            break;
                        }
                        self.aliens[idx].set_kamikaze(true);
                        assigned += 1;
                    }
                }
            }
            FleetState::ActiveCombat => {
                let max_wait = stage_catalog::get_max_attack_wait_frames(self.level_number);
                let mut i = 0;
                while i < self.aliens.len() {
                    if self.aliens[i].stage() == Stage::Cruising {
                        self.aliens[i].decrement_attack_timer();
                        if self.aliens[i].should_launch_attack() {
                            if self.aliens[i].is_kamikaze() && player_pos.is_some() {
                                let target = player_pos.unwrap();
                                self.aliens[i].build_kamikaze_dive(target);
                                sink.sfx(SFX_KAMIKAZE_ALERT, 0.0);
                            } else {
                                self.aliens[i].build_attack();
                            }
                            let wait = if self.aliens[i].is_wanderer() {
                                dist::uniform_int(&mut self.rng, 0, WANDERER_MAX_WAIT_FRAMES)
                            } else {
                                dist::uniform_int(&mut self.rng, 0, max_wait)
                            };
                            self.aliens[i].set_attack_timer(wait);
                        }
                    }
                    i += 1;
                }
            }
        }
    }

    /// AliensManager::Fire twin: the alien fire director. `player_pos` feeds
    /// both the aim vector and the fair-play trap gate (the C++ reads the
    /// wired player pointer for the y coordinate; production always passes
    /// the same starship).
    pub fn fire(&mut self, player_pos: Coord, bombs: &mut BulletsManager) {
        if self.fire_cooldown > 0 {
            self.fire_cooldown -= 1;
            return;
        }
        if self.turning_spacing_cooldown > 0 {
            self.turning_spacing_cooldown -= 1;
        }

        let max_bombs = self.stage_profile.max_bombs;
        let cycle_bomb_mult = self.stage_profile.cycle_bomb_mult;
        let s = self.metrics.scale;
        let fleet_size = 1.max(self.aliens.len() as i32);
        let fire_chance = (FIRE_CHANCE_MIN)
            .max((FIRE_CHANCE_FLEET_FACTOR * fleet_size) / FIRE_CHANCE_FLEET_DIVISOR);
        let player_y = player_pos.y;
        let safe_corridor = (SAFE_EVASION_CORRIDOR_PIXELS * s) as i32;
        let window_w = self.metrics.win_w;
        let speed_threshold = fast_round_f64(self.speed as f64);

        for alien in &self.aliens {
            if bombs.nb() >= max_bombs {
                break;
            }

            if alien.stage() != Stage::Cruising
                && alien.stage() != Stage::Joining
                && self.rng.uniform_int(0, fire_chance - 1) < speed_threshold
            {
                let cannon_pos = alien.cannon_position();
                if bombs.has_bomb_near(
                    cannon_pos,
                    (BOMB_NEAR_DISTANCE_PIXELS * s) as i32,
                ) {
                    continue;
                }

                let individual_variance = BOMB_SPEED_INDIVIDUAL_VARIANCE_BASE
                    + dist::uniform_int(
                        &mut self.rng,
                        0,
                        BOMB_SPEED_INDIVIDUAL_VARIANCE_RANGE,
                    ) as f32
                        / 100.0;
                let base_spd = (
                    (self.speed * BOMB_SPEED_BASE_MULTIPLIER + BOMB_SPEED_BASE_OFFSET)
                        * cycle_bomb_mult
                        * individual_variance
                        * s)
                    .max(BOMB_MIN_SPEED);

                let rad = (alien.angle() + 90.0) * (std::f32::consts::PI / 180.0);
                let mut vy = 2.max(fast_round_f32(rad.sin() * base_spd));
                let max_vx =
                    1.max(fast_round_f32(vy as f32 * BOMB_MAX_HORIZONTAL_SPEED_RATIO));
                let mut vx = fast_round_f32(
                    rad.cos() * base_spd * BOMB_HORIZONTAL_SPEED_MULTIPLIER,
                )
                .clamp(-max_vx, max_vx);

                if bombs.would_trap_player(
                    cannon_pos,
                    Coord::new(vx, vy),
                    player_y,
                    safe_corridor,
                    player_pos.x,
                    window_w,
                ) {
                    vx = 0;
                    if bombs.would_trap_player(
                        cannon_pos,
                        Coord::new(0, vy),
                        player_y,
                        safe_corridor,
                        player_pos.x,
                        window_w,
                    ) {
                        continue;
                    }
                }

                let mut is_turning = false;
                let mut trigger_y = 0;
                let mut target_deflection_deg = 0.0f32;
                // Seeker exhaust color (SSOT palette): slot 0 doubles as the
                // BulletsManager::Add default.
                let mut engine_color = SEEKER_ENGINE_COLORS[0];

                // Vector-guided seeker: probability gate PLUS the fair-play
                // spacing cooldown (short-circuit: a cooling-down fleet
                // consumes NO Bernoulli draw).
                if self.turning_spacing_cooldown <= 0
                    && self.rng.bernoulli(SEEKER_MISSILE_PROBABILITY)
                {
                    is_turning = true;
                    self.turning_spacing_cooldown = dist::uniform_int(
                        &mut self.rng,
                        TURNING_SPACING_COOLDOWN_MIN_FRAMES,
                        TURNING_SPACING_COOLDOWN_MAX_FRAMES,
                    );

                    let vector_speed_variance = BOMB_SPEED_VECTOR_VARIANCE_BASE
                        + dist::uniform_int(
                            &mut self.rng,
                            0,
                            BOMB_SPEED_VECTOR_VARIANCE_RANGE,
                        ) as f32
                            / 100.0;
                    vy = 3.max(fast_round_f32(vy as f32 * vector_speed_variance));

                    let mid_delta = player_y - cannon_pos.y;
                    let pct = dist::uniform_int(
                        &mut self.rng,
                        TURNING_TRIGGER_Y_MIN_PERCENT,
                        TURNING_TRIGGER_Y_MAX_PERCENT,
                    );
                    trigger_y = cannon_pos.y + mid_delta * pct / 100;

                    // Direct aim: atan2(dx, dy) points the seeker at the live
                    // starship, jittered and clamped to the fair-play cone.
                    let dx = (player_pos.x - cannon_pos.x) as f32;
                    let dy = (player_y - cannon_pos.y) as f32;
                    let ideal_angle = dx.atan2(dy) * (180.0 / std::f32::consts::PI);
                    target_deflection_deg = (ideal_angle
                        + dist::uniform_int(
                            &mut self.rng,
                            -DEFLECTION_JITTER_DEG,
                            DEFLECTION_JITTER_DEG,
                        ) as f32)
                        .clamp(-MAX_DEFLECTION_ANGLE_DEG, MAX_DEFLECTION_ANGLE_DEG);

                    let engine_type = dist::uniform_int(&mut self.rng, 0, 3) as usize;
                    engine_color = SEEKER_ENGINE_COLORS[engine_type];
                }

                // Pre-calculated wave special weapon types (StageProfile).
                let is_alien_14 = alien.texture_id() == TextureId::Alien14;
                let is_relativistic =
                    is_alien_14 || self.stage_profile.is_relativistic_wave;
                let is_alien_4 = alien.texture_id() == TextureId::Alien4;
                let is_radioactive =
                    is_alien_4 || self.stage_profile.is_radioactive_wave;

                if is_alien_14 {
                    engine_color = ALIEN14_ENGINE_COLOR; // relativistic propulsion
                } else if is_alien_4 && is_turning {
                    engine_color = ALIEN4_RADIO_ENGINE_COLOR; // radio emission
                }

                bombs.add(
                    TextureId::Bomb,
                    cannon_pos,
                    Coord::new(vx, vy),
                    is_relativistic,
                    is_turning,
                    trigger_y,
                    target_deflection_deg,
                    is_radioactive,
                    engine_color,
                );
                self.fire_cooldown = ALIEN_FIRE_COOLDOWN;
            }
        }
    }

    /// NukeAll twin: one gold debris burst, one score credit and one banner
    /// per alien. The score rides the same optional wiring as the kill chain:
    /// production passes its `Score*` member, harnesses without an economy
    /// pass `None` and the awards vanish with it.
    pub fn nuke_all(
        &mut self,
        explosions: &mut ExplosionsManager,
        sink: &mut EventSink,
        mut score: Option<&mut Score>,
    ) {
        let nuke_text = format!("+{SCORE_PER_ALIEN_NUKED}");
        let positions: Vec<Coord> = self.aliens.iter().map(|a| a.position()).collect();
        for pos in positions {
            explosions.add(pos, Coord::new(0, 0), 255, 230, 90, 36);
            if let Some(sc) = score.as_deref_mut() {
                sc.add(SCORE_PER_ALIEN_NUKED);
            }
            sink.floating_text(
                pos,
                &nuke_text,
                255,
                215,
                0,
                FLOATING_TEXT_NUKE_HIT_FONT_SIZE,
                72,
            );
        }
        self.aliens.clear();
    }

    /// DoBulletsCollisions twin: y-band rejection, warp-transit
    /// invulnerability, the Alien14 emergency warp, the Alien4 decay step and
    /// the kill chain (pop tier, debris, banner, bonus economy, swap-pop).
    pub fn do_bullets_collisions(
        &mut self,
        bullets: &mut BulletsManager,
        explosions: &mut ExplosionsManager,
        bonus: &mut BonusManager,
        sink: &mut EventSink,
        player: Option<&Player>,
        mut score: Option<&mut Score>,
    ) -> CollisionSummary {
        let mut summary = CollisionSummary::default();
        if bullets.nb() <= 0 || self.aliens.is_empty() {
            return summary;
        }

        let (min_b_y, max_b_y) = bullets.y_bounds();
        let half_screen_w = self.metrics.win_w as f32 * 0.5;
        let inv_half_w = if half_screen_w > 0.0 {
            1.0 / half_screen_w
        } else {
            1.0
        };

        let is_stage_4 = self.stage_profile.is_radioactive_wave;
        let mut i = 0usize;
        while i < self.aliens.len() {
            let alien_y = self.aliens[i].position().y;
            let half_h =
                (self.aliens[i].height() as f32 * COLLISION_HITBOX_SCALE) as i32
                    + COLLISION_BAND_MARGIN_PIXELS;

            // Y-band early rejection: no bullet shares this alien's band.
            if alien_y + half_h < min_b_y || alien_y - half_h > max_b_y {
                i += 1;
                continue;
            }

            // Warp-transit invulnerability: colliders are deactivated mid-jump.
            if self.aliens[i].texture_id() == TextureId::Alien14
                && self.aliens[i].is_warping()
            {
                i += 1;
                continue;
            }

            let hit = {
                let go: &GameObject = self.aliens[i].game_object();
                bullets.do_collisions(go, 1)
            };
            if hit != 0 {
                // Sole death condition: the Alien14 dies iff it is NOT on a
                // warp trajectory and evasions are exhausted; otherwise the
                // hit only triggers an emergency jump (the bullet is still
                // consumed by the collision pass above).
                if self.aliens[i].texture_id() == TextureId::Alien14
                    && self.aliens[i].warp_evasions_remaining() > 0
                {
                    let occ = self.build_occupancy_bitset(Some(i));
                    let px = player.map_or(0, |p| p.position().x);
                    let py = player.map_or(0, |p| p.position().y);
                    let nconvoys = self.nconvoys;
                    self.aliens[i].trigger_spacetime_warp(&occ, px, py, nconvoys, sink);
                    sink.sfx(SFX_POWERUP_SPEED, 0.0);
                    i += 1;
                    continue;
                }
                if self.aliens[i].texture_id() == TextureId::Alien4
                    && self.aliens[i].can_trigger_alpha_decay()
                {
                    self.aliens[i].trigger_alpha_decay();
                    let pan_hit = (self.aliens[i].position().x as f32 - half_screen_w)
                        * inv_half_w;
                    sink.sfx(SFX_POP_LIGHT, pan_hit);
                    explosions.add(
                        self.aliens[i].position(),
                        Coord::new(0, -2),
                        45,
                        255,
                        95,
                        26,
                    );
                    let decay_text = format!("+{SCORE_PER_ALIEN_HIT} ALPHA DECAY!");
                    sink.floating_text(
                        self.aliens[i].position(),
                        &decay_text,
                        50,
                        255,
                        120,
                        FLOATING_TEXT_HIT_FONT_SIZE,
                        72,
                    );
                    sink.flash(40, 255, 100, 6);
                    summary.regular_hits += 1;
                    summary.score_earned += SCORE_PER_ALIEN_HIT;
                    summary.total_hits += 1;
                    i += 1;
                    continue;
                }

                let pan =
                    (self.aliens[i].position().x as f32 - half_screen_w) * inv_half_w;

                let is_kamikaze = self.aliens[i].is_kamikaze();
                if is_kamikaze {
                    sink.sfx(SFX_KAMIKAZE_EXPLODE, pan);
                    summary.kamikaze_hits += 1;
                    summary.score_earned += SCORE_PER_KAMIKAZE_ALIEN_HIT;
                    sink.trauma(0.25);
                } else {
                    summary.regular_hits += 1;
                    summary.score_earned += SCORE_PER_ALIEN_HIT;
                    let tid = self.aliens[i].texture_id();
                    if tid == TextureId::Alien15
                        || tid == TextureId::Alien14
                        || tid == TextureId::Alien13
                        || tid == TextureId::Alien4
                    {
                        sink.sfx(SFX_POP_HEAVY, pan);
                    } else if tid >= TextureId::Alien5 {
                        sink.sfx(SFX_POP_MEDIUM, pan);
                    } else {
                        sink.sfx(SFX_POP_LIGHT, pan);
                    }
                }

                let hit_pos = self.aliens[i].position();
                let hit_text = if is_stage_4 {
                    explosions.add(hit_pos, Coord::new(0, -3), 45, 255, 95, 38);
                    let score = if is_kamikaze {
                        SCORE_PER_KAMIKAZE_ALIEN_HIT
                    } else {
                        SCORE_PER_ALIEN_HIT
                    };
                    format!("+{score} RADIUM")
                } else {
                    explosions.add(hit_pos, Coord::new(0, -4), 255, 255, 200, 24);
                    if is_kamikaze {
                        format!("+{SCORE_PER_KAMIKAZE_ALIEN_HIT} KAMIKAZE")
                    } else {
                        format!("+{SCORE_PER_ALIEN_HIT}")
                    }
                };
                let (r, g, b) = if is_stage_4 {
                    (50, 255, 120)
                } else if is_kamikaze {
                    (255, 140, 40)
                } else {
                    (255, 215, 0)
                };
                sink.floating_text(
                    hit_pos,
                    &hit_text,
                    r,
                    g,
                    b,
                    FLOATING_TEXT_HIT_FONT_SIZE,
                    72,
                );

                self.create_bonus_maybe(hit_pos, bonus, player, score.as_deref_mut());
                summary.total_hits += 1;

                // Swap-and-Pop O(1): the fallen alien leaves the dense list
                // exactly like the C++ move-assign + pop_back.
                self.aliens.swap_remove(i);
            } else {
                i += 1;
            }
        }
        summary
    }

    /// CreateBonusMaybe twin: the pre-decrement gate (--bonus_wait > 0) means
    /// a non-positive wait keeps counting down; only the first crossing rolls.
    fn create_bonus_maybe(
        &mut self,
        pos: Coord,
        bonus: &mut BonusManager,
        player: Option<&Player>,
        mut score: Option<&mut Score>,
    ) {
        if !self.bonus_allowed_this_level {
            return;
        }
        if self.bonus_spawned_this_level {
            return;
        }
        self.bonus_wait -= 1;
        if self.bonus_wait > 0 {
            return;
        }

        let chosen = self.roll_smart_bonus_type(player, score.as_deref());
        if chosen == BonusType::None {
            return;
        }
        // Record BEFORE the drop lands (production order): the type locks out
        // for the rest of the 15-wave stage cycle the moment it is chosen.
        if let Some(s) = score.as_deref_mut() {
            s.record_bonus_spawned(chosen as i32);
        }
        bonus.add(pos, chosen);
        self.bonus_spawned_this_level = true;
    }

    /// RollSmartBonusType twin, `Score*` and all: with a wired score the
    /// stage cap (5 spawns) short-circuits BEFORE any draw, and the cycle
    /// mask zeroes the weight of every type already dropped this stage --
    /// a `None` score opens every gate (the nullptr wiring of the Fatia-4
    /// fleet corpus). Player-maxed upgrades zero their weights either way.
    /// The weighted Lemire draw and the fixed fire->multi->speed->shield->
    /// nuke accumulator order are the contract's RNG-visible surface.
    fn roll_smart_bonus_type(&mut self, player: Option<&Player>, score: Option<&Score>) -> BonusType {
        if score.is_some_and(|s| s.stage_bonuses_spawned_count() >= MAX_BONUSES_PER_STAGE) {
            return BonusType::None;
        }
        let can = |t: BonusType| -> bool {
            score.map_or(true, |s| s.can_spawn_bonus_type(t as i32))
        };
        let w_fire = if player.is_some_and(|p| p.is_fire_maxed()) || !can(BonusType::ExtraFire) {
            0
        } else {
            BONUS_WEIGHT_FIRE
        };
        let w_multi = if player.is_some_and(|p| p.is_multi_maxed()) || !can(BonusType::ExtraMulti) {
            0
        } else {
            BONUS_WEIGHT_MULTI
        };
        let w_speed = if player.is_some_and(|p| p.is_speed_maxed()) || !can(BonusType::ExtraSpeed) {
            0
        } else {
            BONUS_WEIGHT_SPEED
        };
        let w_shield = if player
            .is_some_and(|p| p.is_shield_gated() || p.is_shield_maxed())
            || !can(BonusType::ExtraShield)
        {
            0
        } else {
            BONUS_WEIGHT_SHIELD
        };
        let w_nuke = if can(BonusType::ExtraNuke) { BONUS_WEIGHT_NUKE } else { 0 };

        let total_weight = w_fire + w_multi + w_speed + w_shield + w_nuke;
        if total_weight <= 0 {
            return BonusType::None;
        }

        let roll = dist::uniform_int(&mut self.rng, 0, total_weight - 1);
        let mut acc = 0;
        if w_fire > 0 {
            acc += w_fire;
            if acc > roll {
                return BonusType::ExtraFire;
            }
        }
        if w_multi > 0 {
            acc += w_multi;
            if acc > roll {
                return BonusType::ExtraMulti;
            }
        }
        if w_speed > 0 {
            acc += w_speed;
            if acc > roll {
                return BonusType::ExtraSpeed;
            }
        }
        if w_shield > 0 {
            acc += w_shield;
            if acc > roll {
                return BonusType::ExtraShield;
            }
        }
        if w_nuke > 0 {
            acc += w_nuke;
            if acc > roll {
                return BonusType::ExtraNuke;
            }
        }
        BonusType::None
    }

    /// Golden-vector observability (Fatia 11): the per-wave bonus drop gate
    /// -- the `ent.fb.*` rows pin the wait countdown and the Bernoulli
    /// allowance so a capped roll is distinguishable from a silent wave.
    pub fn bonus_allowed_this_level(&self) -> bool {
        self.bonus_allowed_this_level
    }

    pub fn bonus_wait(&self) -> i32 {
        self.bonus_wait
    }

    pub fn bonus_spawned_this_level(&self) -> bool {
        self.bonus_spawned_this_level
    }

    pub fn alien_count(&self) -> usize {
        self.aliens.len()
    }

    pub fn get_alien(&self, i: usize) -> &Alien {
        &self.aliens[i]
    }

    pub fn base_cruise(&self) -> Coord {
        self.base_cruise
    }

    /// Read-only fleet internals for the ent.rz.fleet rows (Fatia 13).
    pub fn max_convoy_size(&self) -> i32 {
        self.max_convoy_size
    }

    pub fn original_max_convoy_size(&self) -> i32 {
        self.original_max_convoy_size
    }

    pub fn min_cruise_x(&self) -> i32 {
        self.min_cruise_x
    }

    pub fn max_cruise_x(&self) -> i32 {
        self.max_cruise_x
    }

    /// Twin of the C++ `AliensManager::SetMetrics` POINTER ALIAS: the shell
    /// pushes one snapshot before every resize cascade and each trajectory
    /// reads it live. The Rust copies are re-pushed here, the same instant.
    pub fn set_metrics(&mut self, m: ViewportMetrics) {
        self.metrics = m;
        for alien in &mut self.aliens {
            alien.set_metrics(m);
        }
    }

    /// Twin of AliensManager::OnResize (managers.cc): recompute the convoy
    /// metrics against the NEW viewport (already distributed via
    /// `set_metrics`), cap by the fixed grid capacity for extreme aspects,
    /// rescale the cruise base x by float FastRound and re-anchor y on the
    /// static grid table, re-derive the cruise bounds, clamp, then cascade
    /// into every alien. The base-cruise push before each alien mirrors the
    /// C++ alias `Trajectory::CruiseTarget` reads through during the re-dock.
    pub fn on_resize(&mut self, rx: f32, ry: f32) {
        self.original_max_convoy_size = self.get_max_convoy_size();
        self.max_convoy_size = (if self.wanderers_allowed_cycle {
            self.original_max_convoy_size + RANDOM_WANDERERS_COUNT
        } else {
            self.original_max_convoy_size
        })
        .min(MAX_GRID_COLS as i32);
        self.base_cruise.x = fast_round_f32(self.base_cruise.x as f32 * rx);
        self.base_cruise.y = self.grid.base_cruise_y;
        self.update_cruise_bounds();
        self.base_cruise.x = self.base_cruise.x.clamp(self.min_cruise_x, self.max_cruise_x);
        let bc = (self.base_cruise.x, self.base_cruise.y);
        for alien in &mut self.aliens {
            alien.set_base_cruise(bc);
            alien.on_resize(rx, ry);
        }
    }

    /// Finished twin: the armada transitioned to combat and every ship fell.
    pub fn finished(&self) -> bool {
        self.fleet_state == FleetState::ActiveCombat && self.aliens.is_empty()
    }
}
