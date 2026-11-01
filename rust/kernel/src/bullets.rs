//! Port of the projectile half of src/entities/managers.{h,cc}:
//! ProjectileSlot, ExhaustParticle and BulletsManager (player bullets, alien
//! bombs, seekers, relativistic and turning variants). The dense-active-index
//! pool layout and the swap-pop removal order are load-bearing: the golden
//! vectors replay GetActive(i)/GetActiveExhaust(i) observations, so iteration
//! order must match the C++ exactly.
//!
//! Scope notes (RUST.md slice 4b): Draw/OnResize/RecordWarpGhost are
//! presentation-only and intentionally absent; `const Pix*` becomes a
//! TextureId resolved through textures::dims.

use crate::dist;
use crate::game_object::GameObject;
use crate::mathx::{fast_round_f32, Coord};
use crate::path::Vec2f;
use crate::random_stream::RandomStream;
use crate::textures::{dims, TextureId};
use crate::trajectory::ViewportMetrics;

/// GameRules::Combat::kMaxProjectiles
const MAX_BULLETS: usize = 512;
/// GameRules::Combat::kMaxExhaustParticles
const MAX_EXHAUST: usize = 128;

// GameRules::Player / GameRules::Combat constants (golden-vector policed
// copies of src/core/constants.h; the corpus compares them every cargo test).
const COLLISION_HITBOX_SCALE: f32 = 0.65;
const WAVE_PHASE_MAX_DEGREES: i32 = 360;
const REL_OSC_AMPLITUDE: f32 = 2.2;
const REL_OSC_FREQUENCY: f32 = 0.18;
const TURN_TOTAL_FRAMES: f32 = 18.0;
const TURNING_BOMB_TURN_TIMER_FRAMES: i32 = 20;
const TURNING_BOMB_REAR_OFFSET_RATIO: f32 = 0.44;
const TURNING_BOMB_LATERAL_SPREAD_SCALE: f32 = 3.6;
const EXHAUST_SPEED_BASE: f32 = 1.6;
const EXHAUST_SPEED_VARIANCE_RANGE: i32 = 99;
const EXHAUST_LATERAL_SPREAD_RANGE: i32 = 50;
const EXHAUST_SPREAD_JITTER_RANGE: i32 = 20;
const EXHAUST_LIFE_MIN_FRAMES: i32 = 7;
const EXHAUST_LIFE_MAX_FRAMES: i32 = 10;
const TRAP_CHECK_SCREEN_EDGE_MARGIN_PIXELS: i32 = 40;
/// kSeekerEngineColors[0] -- the C++ Add() defaults and alien fire share it.
pub const SEEKER_ENGINE_COLOR: (u8, u8, u8) = (210, 60, 255);

/// Trail particle behind completed turning bombs (ExhaustParticle twin).
#[derive(Clone, Copy)]
pub struct ExhaustParticle {
    pub pos: Vec2f,
    pub vel: Vec2f,
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub life: i32,
    pub max_life: i32,
    pub active: bool,
}

impl Default for ExhaustParticle {
    fn default() -> Self {
        Self {
            pos: Vec2f::new(0.0, 0.0),
            vel: Vec2f::new(0.0, 0.0),
            r: 255,
            g: 180,
            b: 40,
            life: 0,
            max_life: 10,
            active: false,
        }
    }
}

impl ExhaustParticle {
    fn step(&mut self) {
        if self.active {
            self.pos = self.pos.add(self.vel);
            self.vel = self.vel.mul(0.94);
            self.life -= 1;
            if self.life <= 0 {
                self.active = false;
            }
        }
    }
}

/// One pooled projectile (ProjectileSlot twin). `tex` replaces `const Pix*`;
/// dimensions come from textures::dims. The per-slot RNG drives the seeker
/// exhaust draws and must keep the C++ consumption order.
pub struct ProjectileSlot {
    pub tex: TextureId,
    pub pos: Coord,
    pub speed: Coord,
    pub fx: f32,
    pub fy: f32,
    pub prev_fx: f32,
    pub prev_fy: f32,
    pub active: bool,
    pub relativistic: bool,
    pub rng: RandomStream,
    pub radioactive: bool,
    pub wave_phase: i32,
    pub is_turning_bomb: bool,
    pub turn_completed: bool,
    pub turn_trigger_y: i32,
    pub turn_timer: i32,
    pub bomb_render_angle: f32,
    pub deflection_angle_deg: f32,
    pub speed_magnitude: f32,
    pub engine_r: u8,
    pub engine_g: u8,
    pub engine_b: u8,
    pub hitbox_w: i32,
    pub hitbox_h: i32,
}

impl Default for ProjectileSlot {
    fn default() -> Self {
        Self {
            tex: TextureId::None,
            pos: Coord::new(0, 0),
            speed: Coord::new(0, 0),
            fx: 0.0,
            fy: 0.0,
            prev_fx: 0.0,
            prev_fy: 0.0,
            active: false,
            relativistic: false,
            // C++ default-constructs RandomStream with seed 0xA341316C.
            rng: RandomStream::new(0xA341316C),
            radioactive: false,
            wave_phase: 0,
            is_turning_bomb: false,
            turn_completed: false,
            turn_trigger_y: 0,
            turn_timer: 0,
            bomb_render_angle: 0.0,
            deflection_angle_deg: 0.0,
            speed_magnitude: 0.0,
            engine_r: 210,
            engine_g: 60,
            engine_b: 255,
            hitbox_w: 0,
            hitbox_h: 0,
        }
    }
}

impl ProjectileSlot {
    /// Twin of ProjectileSlot::Move. Exhaust bookkeeping flows through the
    /// manager-owned pool/count (C++ passes them by reference).
    fn step(
        &mut self,
        exhaust_pool: &mut [ExhaustParticle; MAX_EXHAUST],
        active_exhaust: &mut [u16; MAX_EXHAUST],
        active_exhaust_count: &mut i32,
        player_pos: Coord,
        scale: f32,
    ) {
        if !self.active {
            return;
        }
        self.prev_fx = self.fx;
        self.prev_fy = self.fy;

        if self.is_turning_bomb {
            if !self.turn_completed && self.pos.y >= self.turn_trigger_y {
                if self.turn_timer > 0 {
                    // Seeker side commitment (fair-play telegraph): on the
                    // first spin frame the deflection re-signs toward the
                    // side the starship currently occupies.
                    if self.turn_timer == TURNING_BOMB_TURN_TIMER_FRAMES && player_pos.x >= 0 {
                        let side_dx = (player_pos.x - self.pos.x) as f32;
                        if side_dx != 0.0 {
                            self.deflection_angle_deg =
                                side_dx.signum() * self.deflection_angle_deg.abs();
                        }
                    }
                    self.turn_timer -= 1;
                    let progress = 1.0 - (self.turn_timer as f32 / TURN_TOTAL_FRAMES);
                    let final_render_angle = -self.deflection_angle_deg;
                    self.bomb_render_angle = progress * 360.0 + final_render_angle;
                    self.fy += 1.0;
                    self.pos.y = fast_round_f32(self.fy);
                    return; // spin frames skip x-integration and exhaust
                } else {
                    self.turn_completed = true;
                    self.bomb_render_angle = -self.deflection_angle_deg;
                    let rad = self.deflection_angle_deg * (std::f32::consts::PI / 180.0);
                    self.speed.x = fast_round_f32(rad.sin() * self.speed_magnitude);
                    self.speed.y =
                        3.max(fast_round_f32(rad.cos() * self.speed_magnitude));
                }
            }

            // Exhaust randomness rides the slot's own stream through the native
            // modulo-rejection `uniform_int` — the same `RandomStream::UniformInt`
            // the C++ calls here. It is deliberately NOT `dist::uniform_int`: that
            // twin models libstdc++'s Lemire algorithm, which only the wave-phase
            // draw in `add` uses (`std::uniform_int_distribution` over the manager
            // stream). The two algorithms reject differently, so swapping them
            // shifts every later draw and desynchronizes the golden vectors.
            if self.turn_completed && *active_exhaust_count < MAX_EXHAUST as i32 {
                let current_deflection_rad =
                    -self.bomb_render_angle * (std::f32::consts::PI / 180.0);
                let (_tex_w, tex_h) = dims(self.tex);
                let h = tex_h as f32;
                let rear_offset = h * TURNING_BOMB_REAR_OFFSET_RATIO;

                let mut rear_x = self.fx - current_deflection_rad.sin() * rear_offset;
                let mut rear_y = self.fy - current_deflection_rad.cos() * rear_offset;

                let lateral_spread = (self.rng.uniform_int(
                    -EXHAUST_LATERAL_SPREAD_RANGE,
                    EXHAUST_LATERAL_SPREAD_RANGE - 1,
                ) as f32
                    / EXHAUST_LATERAL_SPREAD_RANGE as f32)
                    * (TURNING_BOMB_LATERAL_SPREAD_SCALE * scale);
                rear_x += current_deflection_rad.cos() * lateral_spread;
                rear_y -= current_deflection_rad.sin() * lateral_spread;

                let exhaust_speed = EXHAUST_SPEED_BASE
                    + self.rng.uniform_int(0, EXHAUST_SPEED_VARIANCE_RANGE) as f32
                        / 110.0;
                let spread_jitter = self.rng.uniform_int(
                    -EXHAUST_SPREAD_JITTER_RANGE,
                    EXHAUST_SPREAD_JITTER_RANGE - 1,
                ) as f32
                    / 100.0;
                let jet_vx = -(current_deflection_rad + spread_jitter).sin() * exhaust_speed;
                let jet_vy = -(current_deflection_rad + spread_jitter).cos() * exhaust_speed;

                for ep_idx in 0..MAX_EXHAUST {
                    let ep = &mut exhaust_pool[ep_idx];
                    if !ep.active {
                        ep.pos = Vec2f::new(rear_x, rear_y);
                        ep.vel = Vec2f::new(jet_vx, jet_vy);
                        ep.r = self.engine_r;
                        ep.g = self.engine_g;
                        ep.b = self.engine_b;
                        let life = self.rng.uniform_int(
                            EXHAUST_LIFE_MIN_FRAMES,
                            EXHAUST_LIFE_MAX_FRAMES,
                        );
                        ep.life = life;
                        ep.max_life = life;
                        ep.active = true;
                        active_exhaust[*active_exhaust_count as usize] = ep_idx as u16;
                        *active_exhaust_count += 1;
                        break;
                    }
                }
            }
        }

        self.fx += self.speed.x as f32;
        self.fy += self.speed.y as f32;
        self.pos.x = fast_round_f32(self.fx);
        self.pos.y = fast_round_f32(self.fy);
    }

    /// Twin of ProjectileSlot::Out: despawn 60 px past any screen edge.
    fn out(&self, win_w: i32, win_h: i32) -> bool {
        let (w, h) = dims(self.tex);
        let hw = w / 2;
        let hh = h / 2;
        self.pos.y + hh < -60
            || self.pos.y - hh >= win_h + 60
            || self.pos.x + hw < -60
            || self.pos.x - hw >= win_w + 60
    }
}

/// BulletsManager twin: one instance serves player bullets OR alien bombs
/// (the C++ shell constructs two). Dense `active_ids` over a fixed pool with
/// swap-pop removal keeps observation order identical to C++.
pub struct BulletsManager {
    pool: Box<[ProjectileSlot; MAX_BULLETS]>,
    active_ids: [u16; MAX_BULLETS],
    exhaust_pool: Box<[ExhaustParticle; MAX_EXHAUST]>,
    active_exhaust_ids: [u16; MAX_EXHAUST],
    rng: RandomStream,
    active_count: i32,
    active_exhaust_count: i32,
    cached_min_y: i32,
    cached_max_y: i32,
    metrics: ViewportMetrics,
}

impl BulletsManager {
    pub fn new(seed: u32) -> Self {
        Self {
            // ProjectileSlot holds a RandomStream (no Copy) -- from_fn
            // default-constructs each slot like the C++ aggregate init.
            pool: Box::new(std::array::from_fn(|_| ProjectileSlot::default())),
            active_ids: [0; MAX_BULLETS],
            exhaust_pool: Box::new([ExhaustParticle::default(); MAX_EXHAUST]),
            active_exhaust_ids: [0; MAX_EXHAUST],
            rng: RandomStream::new(seed),
            active_count: 0,
            active_exhaust_count: 0,
            cached_min_y: i32::MAX,
            cached_max_y: i32::MIN,
            metrics: ViewportMetrics { win_w: 1280, win_h: 720, scale: 1.0 },
        }
    }

    pub fn seed_random(&mut self, seed: u32) {
        self.rng.reseed(seed);
    }

    pub fn set_metrics(&mut self, m: ViewportMetrics) {
        self.metrics = m;
    }

    /// Twin of BulletsManager::Add. The wave_phase draw (uniform_int over the
    /// std-distribution twin) consumes the manager RNG exactly like the C++
    /// `std::uniform_int_distribution<int>(0, 359)(rng_)`.
    #[allow(clippy::too_many_arguments)]
    pub fn add(
        &mut self,
        tex: TextureId,
        pos: Coord,
        speed: Coord,
        relativistic: bool,
        is_turning: bool,
        trigger_y: i32,
        deflection_deg: f32,
        radioactive: bool,
        engine: (u8, u8, u8),
    ) {
        if self.active_count >= MAX_BULLETS as i32 {
            return;
        }
        for idx in 0..MAX_BULLETS {
            if !self.pool[idx].active {
                let b = &mut self.pool[idx];
                b.tex = tex;
                b.pos = pos;
                b.speed = speed;
                b.fx = pos.x as f32;
                b.fy = pos.y as f32;
                b.prev_fx = b.fx;
                b.prev_fy = b.fy;
                b.active = true;
                b.relativistic = relativistic;
                b.radioactive = radioactive;
                b.wave_phase =
                    dist::uniform_int(&mut self.rng, 0, WAVE_PHASE_MAX_DEGREES - 1);
                b.is_turning_bomb = is_turning;
                b.turn_completed = false;
                b.turn_trigger_y = trigger_y;
                b.turn_timer = TURNING_BOMB_TURN_TIMER_FRAMES;
                b.bomb_render_angle = 0.0;
                b.deflection_angle_deg = deflection_deg;
                b.speed_magnitude =
                    ((speed.x * speed.x + speed.y * speed.y) as f32).sqrt();
                b.engine_r = engine.0;
                b.engine_g = engine.1;
                b.engine_b = engine.2;
                let (w, h) = dims(tex);
                b.hitbox_w = (w as f32 * COLLISION_HITBOX_SCALE) as i32;
                b.hitbox_h = (h as f32 * COLLISION_HITBOX_SCALE) as i32;

                self.cached_min_y = self.cached_min_y.min(pos.y);
                self.cached_max_y = self.cached_max_y.max(pos.y);
                self.active_ids[self.active_count as usize] = idx as u16;
                self.active_count += 1;
                return;
            }
        }
    }

    /// Twin of BulletsManager::Move: exhaust first (swap-pop), then each live
    /// projectile (step + relativistic oscillation + despawn check), while
    /// refreshing the cached y-bounds used by the kamikaze proximity probe.
    pub fn advance(&mut self, player_pos: Coord) {
        self.cached_min_y = 999999;
        self.cached_max_y = -999999;

        let mut i = 0i32;
        while i < self.active_exhaust_count {
            let idx = self.active_exhaust_ids[i as usize] as usize;
            self.exhaust_pool[idx].step();
            if !self.exhaust_pool[idx].active {
                self.active_exhaust_count -= 1;
                self.active_exhaust_ids[i as usize] =
                    self.active_exhaust_ids[self.active_exhaust_count as usize];
            } else {
                i += 1;
            }
        }

        let (win_w, win_h, scale) =
            (self.metrics.win_w, self.metrics.win_h, self.metrics.scale);
        let mut i = 0i32;
        while i < self.active_count {
            let idx = self.active_ids[i as usize] as usize;
            self.pool[idx].step(
                &mut self.exhaust_pool,
                &mut self.active_exhaust_ids,
                &mut self.active_exhaust_count,
                player_pos,
                scale,
            );
            if self.pool[idx].relativistic {
                let b = &mut self.pool[idx];
                b.wave_phase += 1;
                b.fx += ((b.wave_phase as f32) * REL_OSC_FREQUENCY).sin()
                    * REL_OSC_AMPLITUDE;
                b.pos.x = fast_round_f32(b.fx);
            }
            if self.pool[idx].out(win_w, win_h) {
                self.pool[idx].active = false;
                self.active_count -= 1;
                self.active_ids[i as usize] = self.active_ids[self.active_count as usize];
            } else {
                let y = self.pool[idx].pos.y;
                if y < self.cached_min_y {
                    self.cached_min_y = y;
                }
                if y > self.cached_max_y {
                    self.cached_max_y = y;
                }
                i += 1;
            }
        }
    }

    /// Twin of BulletsManager::DoCollisions: AABB overlap on the 0.65-scaled
    /// hitboxes, hoisted other-extents, swap-pop removal, capped hit count.
    pub fn do_collisions(&mut self, other: &GameObject, max: i32) -> i32 {
        if self.active_count <= 0 {
            return 0;
        }
        let other_x = other.pos.x;
        let other_y = other.pos.y;
        let hh2 = (other.height() as f32 * COLLISION_HITBOX_SCALE) as i32;
        let hw2 = (other.width() as f32 * COLLISION_HITBOX_SCALE) as i32;

        let mut res = 0;
        let mut i = 0i32;
        while i < self.active_count {
            let idx = self.active_ids[i as usize] as usize;
            if !self.pool[idx].active {
                i += 1;
                continue;
            }
            let b = &self.pool[idx];
            if (b.pos.y - other_y).abs() * 2 >= b.hitbox_h + hh2 {
                i += 1;
                continue;
            }
            if (b.pos.x - other_x).abs() * 2 < b.hitbox_w + hw2 {
                self.pool[idx].active = false;
                self.active_count -= 1;
                self.active_ids[i as usize] = self.active_ids[self.active_count as usize];
                res += 1;
                if res >= max {
                    break;
                }
            } else {
                i += 1;
            }
        }
        res
    }

    /// Twin of BulletsManager::HasBombNear (kamikaze shield probe).
    pub fn has_bomb_near(&self, pos: Coord, min_dist_px: i32) -> bool {
        if self.active_count <= 0 {
            return false;
        }
        let min_sq = min_dist_px * min_dist_px;
        for i in 0..self.active_count {
            let b = &self.pool[self.active_ids[i as usize] as usize];
            let dx = pos.x - b.pos.x;
            let dy = pos.y - b.pos.y;
            if dx * dx + dy * dy < min_sq {
                return true;
            }
            if dx.abs() < 26 && dy.abs() < 85 {
                return true;
            }
        }
        false
    }

    /// Twin of BulletsManager::HasProjectileWithinRadius (Alien14 warp trigger
    /// probe): only descending projectiles within 12 px above the alien count.
    pub fn has_projectile_within_radius(&self, pos: Coord, trigger_dist: f32) -> bool {
        if self.active_count <= 0 || trigger_dist <= 0.0 {
            return false;
        }
        let dist_sq_threshold = trigger_dist * trigger_dist;
        for i in 0..self.active_count {
            let b = &self.pool[self.active_ids[i as usize] as usize];
            if !b.active || b.speed.y >= 0 || b.pos.y < pos.y - 12 {
                continue;
            }
            let dx = (b.pos.x - pos.x) as f32;
            let dy = (b.pos.y - pos.y) as f32;
            if dx * dx + dy * dy <= dist_sq_threshold {
                return true;
            }
        }
        false
    }

    /// Twin of BulletsManager::WouldTrapPlayer (Alien4 salvo fairness gate):
    /// integer flight-time intersection at the player's row, edge trap, and
    /// corridor / crossing checks against every descending projectile.
    pub fn would_trap_player(
        &self,
        spawn_pos: Coord,
        speed: Coord,
        player_y: i32,
        safe_corridor: i32,
        player_x: i32,
        window_w: i32,
    ) -> bool {
        if speed.y <= 0 || self.active_count <= 0 || player_y <= spawn_pos.y {
            return false;
        }
        // C++ int division truncates toward zero; Rust i32 `/` matches.
        let t_new = (player_y - spawn_pos.y) / speed.y;
        let x_new = spawn_pos.x + speed.x * t_new;

        let edge = TRAP_CHECK_SCREEN_EDGE_MARGIN_PIXELS;
        if x_new < edge || x_new > window_w - edge {
            return true;
        }

        for i in 0..self.active_count {
            let b = &self.pool[self.active_ids[i as usize] as usize];
            if b.speed.y <= 0 {
                continue;
            }
            let t_exist = (player_y - b.pos.y) / b.speed.y;
            let x_exist = b.pos.x + b.speed.x * t_exist;

            if (t_exist - t_new).abs() < 35 {
                let converging = (b.speed.x > 0 && speed.x < 0)
                    || (b.speed.x < 0 && speed.x > 0);
                let min_separation = if converging {
                    safe_corridor * 2
                } else {
                    safe_corridor
                };
                if (x_exist - x_new).abs() < min_separation {
                    return true;
                }
                if (x_exist < player_x && x_new > player_x)
                    || (x_new < player_x && x_exist > player_x)
                {
                    if (x_exist - x_new).abs() < safe_corridor + 85 {
                        return true;
                    }
                }
            }
        }
        false
    }

    pub fn nb(&self) -> i32 {
        self.active_count
    }

    pub fn get_active(&self, i: usize) -> &ProjectileSlot {
        &self.pool[self.active_ids[i] as usize]
    }

    pub fn active_exhaust_count(&self) -> i32 {
        self.active_exhaust_count
    }

    pub fn get_active_exhaust(&self, i: usize) -> &ExhaustParticle {
        &self.exhaust_pool[self.active_exhaust_ids[i] as usize]
    }

    pub fn y_bounds(&self) -> (i32, i32) {
        (self.cached_min_y, self.cached_max_y)
    }
}
