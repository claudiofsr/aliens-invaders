//! Port of src/entities/explosion.{h,cc}: Particle and ExplosionsManager.
//! The free-list stack (LIFO spawn order), the per-particle RNG draw order in
//! Add and the decay-table velocity solve are all observation-load-bearing:
//! the golden vectors replay ActiveCount()/GetActiveParticle(i) state.
//!
//! Scope notes (RUST.md slice 4b): Draw/OnResize are presentation-only and
//! intentionally absent. Two C++ globals become constructor inputs instead:
//! Config::ScaleDetails (headless: details_level 2 of 4) and the fleet grid
//! width read by Add for max_radius (fixed by the one Recompute(scale) the
//! corpus performs up front, so construction-time binding is identical).

use crate::mathx::{fast_round_f32, Coord};
use crate::path::Vec2f;
use crate::random_stream::RandomStream;
use std::sync::OnceLock;

/// GameRules::Combat pool bound (kMaxParticles).
const MAX_PARTICLES: usize = 4096;

// GameRules::Combat constants (golden-vector policed copies).
const DEBRIS_SCALE_FACTOR: i32 = 8;
const DEBRIS_MIN_PARTICLES: i32 = 20;
const DEBRIS_MAX_PARTICLES: i32 = 200;
const DEBRIS_RADIUS_MULTIPLIER: f32 = 1.0;
const INITIAL_RADIUS_FACTOR: f32 = 0.25;
const EXPANSION_MIN_FACTOR: f32 = 0.35;
const DECAY_EPSILON: f32 = 0.10;
const DRAG: f32 = 0.96;
const RESIDUAL_VELOCITY_FACTOR: f32 = 0.15;
const DECAY_TABLE_SIZE: usize = 128;

/// Geometric decay table: table[k] = sum_{step=0}^{k-1} drag^step, built with
/// the same iterative f32 accumulation the C++ constexpr lambda performs, so
/// every entry is bit-identical.
fn decay_table() -> &'static [f32; DECAY_TABLE_SIZE] {
    static TABLE: OnceLock<[f32; DECAY_TABLE_SIZE]> = OnceLock::new();
    TABLE.get_or_init(|| {
        let mut table = [0.0f32; DECAY_TABLE_SIZE];
        for k in 1..DECAY_TABLE_SIZE {
            let mut sum = 0.0f32;
            let mut term = 1.0f32;
            for _ in 0..k {
                sum += term;
                term *= DRAG;
            }
            table[k] = sum;
        }
        table
    })
}

/// Twin of struct Particle (explosion.h).
#[derive(Clone, Copy)]
pub struct Particle {
    pub pos: Vec2f,
    pub vel: Vec2f,
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: u8,
    pub life: u16,
    pub max_life: u16,
    pub inv_max_life: f32,
    pub active: bool,
}

impl Default for Particle {
    fn default() -> Self {
        Self {
            pos: Vec2f::new(0.0, 0.0),
            vel: Vec2f::new(0.0, 0.0),
            r: 255,
            g: 255,
            b: 255,
            a: 255,
            life: 0,
            max_life: 0,
            inv_max_life: 0.0,
            active: false,
        }
    }
}

/// ExplosionsManager twin: dense active-index list over a LIFO free stack.
pub struct ExplosionsManager {
    pool: Box<[Particle; MAX_PARTICLES]>,
    active_indices: [u16; MAX_PARTICLES],
    free_indices: [u16; MAX_PARTICLES],
    rng: RandomStream,
    details_level: i32,
    max_details_level: i32,
    fleet_width: i32,
    win_scale: f32,
    active_count: usize,
    free_count: usize,
}

impl ExplosionsManager {
    /// `details_level` mirrors the headless Config (2 of 4); `fleet_width`
    /// mirrors GameRules::Fleet::FormationGrid::Width() as fixed by the
    /// corpus-initial Recompute.
    pub fn new(details_level: i32, fleet_width: i32) -> Self {
        let mut free_indices = [0u16; MAX_PARTICLES];
        for (i, slot) in free_indices.iter_mut().enumerate() {
            *slot = i as u16;
        }
        Self {
            pool: Box::new([Particle::default(); MAX_PARTICLES]),
            active_indices: [0; MAX_PARTICLES],
            free_indices,
            rng: RandomStream::new(0x5EED1337),
            details_level,
            max_details_level: 4,
            fleet_width,
            win_scale: 1.0,
            active_count: 0,
            free_count: MAX_PARTICLES,
        }
    }

    pub fn seed_random(&mut self, seed: u32) {
        self.rng.reseed(seed);
    }

    /// Injected viewport snapshot (the C++ twin keeps ViewportMetrics; Nova
    /// reads its scale, Move/Add do not).
    pub fn set_metrics(&mut self, scale: f32) {
        self.win_scale = scale;
    }

    /// Twin of Config::ScaleDetails: integer left-to-right `val * level / max`.
    fn scale_details(&self, val: i32) -> i32 {
        val * self.details_level / self.max_details_level
    }

    /// Twin of ExplosionsManager::Add: debris burst with per-particle draws in
    /// the exact C++ order (angle, r_init, life, expansion factor).
    pub fn add(&mut self, pos: Coord, speed: Coord, r: u8, g: u8, b: u8, duration: i32) {
        let scaled_details = self.scale_details(DEBRIS_SCALE_FACTOR * duration);
        let count = scaled_details.clamp(DEBRIS_MIN_PARTICLES, DEBRIS_MAX_PARTICLES);

        // Final physical radius strictly bounded by the ship's fuselage.
        let max_radius = self.fleet_width as f32 * DEBRIS_RADIUS_MULTIPLIER;

        let mut spawned = 0;
        while self.free_count > 0 && spawned < count {
            self.free_count -= 1;
            let idx = self.free_indices[self.free_count] as usize;
            let p = &mut self.pool[idx];

            // Uniform polar angle (perfectly circular burst).
            let angle = self.rng.uniform_float(0.0, 2.0 * std::f32::consts::PI);
            let cos_a = angle.cos();
            let sin_a = angle.sin();

            // Birth point confined to the ship's core (up to 25% of radius).
            let r_init = self.rng.uniform_float(0.0, max_radius * INITIAL_RADIUS_FACTOR);
            p.pos = Vec2f::new(
                pos.x as f32 + cos_a * r_init,
                pos.y as f32 + sin_a * r_init,
            );

            let life = (duration + self.rng.uniform_int(-3, 3)) as u16;
            p.life = life;
            p.max_life = life;
            p.inv_max_life = if p.max_life > 0 {
                1.0 / p.max_life as f32
            } else {
                0.0
            };

            let life_clamped = (p.life as usize).min(DECAY_TABLE_SIZE - 1);
            let decay_sum = decay_table()[life_clamped];

            let target_expansion =
                (max_radius - r_init) * self.rng.uniform_float(EXPANSION_MIN_FACTOR, 1.0);
            let v_mag = if decay_sum > DECAY_EPSILON {
                target_expansion / decay_sum
            } else {
                1.0
            };

            let residual_vx = speed.x as f32 * RESIDUAL_VELOCITY_FACTOR;
            let residual_vy = speed.y as f32 * RESIDUAL_VELOCITY_FACTOR;
            p.vel = Vec2f::new(cos_a * v_mag + residual_vx, sin_a * v_mag + residual_vy);

            p.r = r;
            p.g = g;
            p.b = b;
            p.a = 255;
            p.active = true;

            self.active_indices[self.active_count] = idx as u16;
            self.active_count += 1;
            spawned += 1;
        }
    }

    /// Twin of TriggerScreenWideNova. RNG consumption is mirrored draw-for-
    /// draw, including the three burst Adds, the pool-filling ring and the
    /// trailing grid loop -- whose four per-cell draws are consumed even when
    /// the pool is already full, because C++ evaluates the Add arguments
    /// before the call (GCC order: right-to-left, so speed.y, speed.x,
    /// pos.y, pos.x).
    pub fn trigger_screen_wide_nova(&mut self, epicenter: Coord, win_w: i32, win_h: i32) {
        let s = self.win_scale;
        self.add(epicenter, Coord::new(0, 0), 255, 255, 240, 75);
        self.add(epicenter, Coord::new(0, (-3.0f32 * s) as i32), 255, 180, 40, 65);
        self.add(epicenter, Coord::new(0, (3.0f32 * s) as i32), 255, 50, 20, 65);

        let max_speed = 10.0 * s;
        let min_speed = 2.0 * s;

        while self.free_count > 0 && self.active_count < MAX_PARTICLES {
            self.free_count -= 1;
            let idx = self.free_indices[self.free_count] as usize;
            let p = &mut self.pool[idx];
            let ang = self.rng.uniform_float(0.0, 2.0 * std::f32::consts::PI);
            let spd = self.rng.uniform_float(min_speed, max_speed);
            p.pos = epicenter.to_vec2f();
            p.vel = Vec2f::new(ang.cos() * spd, ang.sin() * spd);
            p.r = 255;
            p.g = self.rng.uniform_int(100, 240) as u8;
            p.b = 30;
            p.a = 255;
            let life = self.rng.uniform_int(40, 75) as u16;
            p.life = life;
            p.max_life = life;
            p.inv_max_life = if p.max_life > 0 {
                1.0 / p.max_life as f32
            } else {
                0.0
            };
            p.active = true;
            self.active_indices[self.active_count] = idx as u16;
            self.active_count += 1;
        }

        let step_x = fast_round_f32(220.0 * s);
        let step_y = fast_round_f32(180.0 * s);
        let margin = fast_round_f32(80.0 * s);
        let jitter = (30.0f32 * s) as i32;
        let mut x = margin;
        while x < win_w {
            let mut y = margin;
            while y < win_h {
                // GCC argument order (right-to-left, inner-to-outer):
                // speed.y, speed.x, pos.y, pos.x.
                let spd_y = self.rng.uniform_int(-2, 2);
                let spd_x = self.rng.uniform_int(-2, 2);
                let pos_y = self.rng.uniform_int(-jitter, jitter);
                let pos_x = self.rng.uniform_int(-jitter, jitter);
                self.add(
                    Coord::new(x + pos_x, y + pos_y),
                    Coord::new(spd_x, spd_y),
                    255,
                    120,
                    20,
                    45,
                );
                y += step_y;
            }
            x += step_x;
        }
    }

    /// Twin of ExplosionsManager::Move: integrate, drag, fade; expired
    /// particles return to the free stack with swap-pop on the active list.
    pub fn advance(&mut self) {
        if self.active_count == 0 {
            return;
        }
        let mut i = 0usize;
        while i < self.active_count {
            let idx = self.active_indices[i] as usize;
            let p = &mut self.pool[idx];
            p.pos = p.pos.add(p.vel);
            p.vel = p.vel.mul(DRAG);

            if p.life > 0 {
                p.life -= 1;
                let ratio = p.life as f32 * p.inv_max_life;
                p.a = (ratio * 255.0) as u8;
                p.r = (p.r as f32 * 0.985) as u8;
                p.g = (p.g as f32 * 0.955) as u8;
                p.b = (p.b as f32 * 0.955) as u8;
                i += 1;
            } else {
                p.active = false;
                self.free_indices[self.free_count] = idx as u16;
                self.free_count += 1;
                self.active_count -= 1;
                self.active_indices[i] = self.active_indices[self.active_count];
            }
        }
    }

    /// Twin of ExplosionsManager::Clear.
    pub fn clear(&mut self) {
        for i in 0..self.active_count {
            self.pool[self.active_indices[i] as usize].active = false;
        }
        self.active_count = 0;
        self.free_count = MAX_PARTICLES;
        for (i, slot) in self.free_indices.iter_mut().enumerate() {
            *slot = i as u16;
        }
    }

    pub fn active_count(&self) -> usize {
        self.active_count
    }

    pub fn get_active_particle(&self, i: usize) -> &Particle {
        &self.pool[self.active_indices[i] as usize]
    }
}
