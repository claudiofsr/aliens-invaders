//! Starfield (RUST.md Fatia 6): the parallax star pool behind every screen.
//!
//! Port of the pure half of `StarsFields` (src/starfield/stars.cc after the
//! Fatia 6 physical split -- `Draw`, the flare texture and the live `Gfx`
//! window poll stay shell-side in stars_draw.cc). The golden contract is the
//! `ent.stars.*` corpus: a 640-frame `Scroll()` replay covering normal fall,
//! edge respawns, a production-length hyperspace warp, viewport growth
//! (density expansion) and a details drop (density shrink), bit-exact on
//! EVERY column with zero tolerance.
//!
//! Zero tolerance is possible because the transcendentals are FROZEN: the
//! twinkle LUT (sinf harmonics) and the hyperspace warp easing (cosf blend)
//! are recorded into the corpus and embedded below as data -- the same
//! policy as `textures::HEADLESS_DIMS`. The replay path is then pure IEEE
//! arithmetic + RandomStream draws + one correctly-rounded sqrt, so it is
//! platform-independent. Regenerating the corpus on a different glibc
//! re-records the frozen tables; this module's constants must then be
//! re-frozen (the parity test points at the drifted row).
//!
//! RNG discipline: the pool stream is the C++ member `RandomStream rng_`
//! seeded `0xC0531CC1`; `RespawnStar` consumes native `UniformFloat` /
//! `UniformInt` draws in exactly the recorded order (spawn x, optional
//! spawn y, layer roll, fall speed, twinkle phase, twinkle speed, spectral
//! class, then 1-3 color draws).

use crate::mathx::{fast_round_f32, fast_round_f64};
use crate::random_stream::RandomStream;

/// Seed of the starfield's private RandomStream (SSOT: stars.h `rng_`).
pub const STARFIELD_SEED: u32 = 0xC0531CC1;

// GameRules::Starfield / Progression / Simulation constants (SSOT:
// src/core/constants.h). Duplicated by policy: every one is compared against
// the `ent.stars.setup` echo rows on each `cargo test`.
pub const K_BASE_RES_W: f64 = 1280.0;
pub const K_BASE_RES_H: f64 = 720.0;
pub const K_BASE_STAR_COUNT: f64 = 120.0;
pub const K_MIN_STARS: usize = 90;
pub const K_MAX_STARS: usize = 425;
pub const K_FOREGROUND_THRESHOLD: f32 = 0.82;
pub const K_TWINKLE_PHASE_SCALE: f32 = 65536.0;
pub const K_TAU: f32 = 2.0 * std::f32::consts::PI;
pub const K_TWINKLE_FLOOR: f32 = 0.38;
pub const K_TWINKLE_CEIL: f32 = 1.00;
pub const K_TWINKLE_HARMONIC2: f32 = 0.18;
pub const K_TWINKLE_HARMONIC3: f32 = 0.08;
pub const K_TWINKLE_PERIOD_MIN_SEC: f32 = 1.50;
pub const K_TWINKLE_PERIOD_MAX_SEC: f32 = 3.75;
pub const K_SIM_HZ: f32 = 60.0;
pub const K_MID_SPEED_MIN: f32 = 0.10;
pub const K_MID_SPEED_MAX: f32 = 0.15;
pub const K_FG_SPEED_MIN: f32 = 0.20;
pub const K_FG_SPEED_MAX: f32 = 0.25;
/// Derived exactly like the C++ constexprs: tau / (period_sec * sim_hz).
pub const K_TWINKLE_SPEED_MAX: f32 = K_TAU / (K_TWINKLE_PERIOD_MIN_SEC * K_SIM_HZ);
pub const K_TWINKLE_SPEED_MIN: f32 = K_TAU / (K_TWINKLE_PERIOD_MAX_SEC * K_SIM_HZ);
pub const K_WARP_MULT: f32 = 9.0;
pub const K_STAGE_FANFARE_SECONDS: f32 = 6.0;
/// Production warp length: FastRound(kStageFanfareDurationSeconds *
/// kSimulationFrequencyHz) -- the phase transition always triggers with this
/// count, and the frozen easing curve below is recorded for exactly it.
pub const WARP_DURATION_FRAMES: i32 = 360;
/// Detail level -> density factor (default branch = level 2, like C++).
pub const K_DENSITY_FACTORS: [f64; 5] = [0.00, 0.60, 1.50, 2.00, 3.50];

const POOL_CAPACITY: usize = K_MAX_STARS * 2;

/// Frozen twinkle LUT: the recorded output of the C++ static-init table
/// (primary sine + 2nd/3rd harmonics, squared, floor/ceil mapped, x255).
/// Transcendental content pinned as data -- see the module docs.
pub const TWINKLE_ALPHA: [u8; 256] = [
    0xb0, 0xb3, 0xb7, 0xba, 0xbd, 0xbf, 0xc2, 0xc5, 0xc7, 0xc9, 0xcb, 0xcd, 0xcf, 0xd1, 0xd2, 0xd4,
    0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xda, 0xdb, 0xdb, 0xdc, 0xdc, 0xdc, 0xdd, 0xdd, 0xdd, 0xdd,
    0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xde, 0xde, 0xde, 0xde, 0xde, 0xde, 0xde, 0xde,
    0xde, 0xdf, 0xdf, 0xdf, 0xdf, 0xdf, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0,
    0xe0, 0xe0, 0xe0, 0xe0, 0xdf, 0xdf, 0xdf, 0xde, 0xdd, 0xdd, 0xdc, 0xdb, 0xda, 0xd9, 0xd8, 0xd7,
    0xd6, 0xd5, 0xd3, 0xd2, 0xd1, 0xcf, 0xce, 0xcc, 0xca, 0xc9, 0xc7, 0xc5, 0xc3, 0xc2, 0xc0, 0xbe,
    0xbc, 0xba, 0xb9, 0xb7, 0xb5, 0xb3, 0xb2, 0xb0, 0xae, 0xad, 0xab, 0xaa, 0xa8, 0xa7, 0xa5, 0xa4,
    0xa3, 0xa2, 0xa0, 0x9f, 0x9e, 0x9d, 0x9c, 0x9b, 0x9a, 0x99, 0x99, 0x98, 0x97, 0x96, 0x96, 0x95,
    0x94, 0x93, 0x93, 0x92, 0x91, 0x91, 0x90, 0x8f, 0x8f, 0x8e, 0x8d, 0x8c, 0x8b, 0x8b, 0x8a, 0x89,
    0x88, 0x87, 0x86, 0x84, 0x83, 0x82, 0x81, 0x7f, 0x7e, 0x7d, 0x7b, 0x7a, 0x78, 0x77, 0x75, 0x74,
    0x72, 0x71, 0x6f, 0x6e, 0x6c, 0x6b, 0x6a, 0x69, 0x67, 0x66, 0x65, 0x64, 0x63, 0x63, 0x62, 0x62,
    0x61, 0x61, 0x61, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
    0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
    0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
    0x60, 0x61, 0x61, 0x61, 0x62, 0x63, 0x64, 0x65, 0x67, 0x68, 0x6a, 0x6c, 0x6e, 0x71, 0x73, 0x76,
    0x79, 0x7c, 0x7f, 0x82, 0x86, 0x89, 0x8d, 0x90, 0x94, 0x97, 0x9b, 0x9f, 0xa2, 0xa6, 0xa9, 0xad,
];

/// Frozen hyperspace easing, indexed by the REMAINING warp timer after the
/// decrement (359..0): the recorded output of the C++ branch
/// `p < 0.65 ? kHyperspaceWarpSpeedMultiplier : 1 + (mult-1)*(0.5+0.5*cos((p-0.65)/0.35*Pi))`
/// with p = 1 - timer/duration. Bits stored; `f32::from_bits` on read.
pub const WARP_CURVE_BITS: [u32; WARP_DURATION_FRAMES as usize] = [
    0x3f800000, 0x3f8028bc, 0x3f80a2ee, 0x3f816e80, 0x3f828b54, 0x3f83f936,
    0x3f85b7f6, 0x3f87c746, 0x3f8a26d2, 0x3f8cd63c, 0x3f8fd518, 0x3f9322e8,
    0x3f96bf28, 0x3f9aa940, 0x3f9ee098, 0x3fa36482, 0x3fa83442, 0x3fad4f12,
    0x3fb2b430, 0x3fb862b4, 0x3fbe59be, 0x3fc4984a, 0x3fcb1d6e, 0x3fd1e81a,
    0x3fd8f736, 0x3fe0499c, 0x3fe7de38, 0x3fefb3c2, 0x3ff7c900, 0x40000e52,
    0x400456b0, 0x4008bcf0, 0x400d4058, 0x4011e034, 0x40169bbe, 0x401b723f,
    0x402062f6, 0x40256d11, 0x402a8fbf, 0x402fca3e, 0x40351baa, 0x403a8330,
    0x403ffffc, 0x4045911f, 0x404b35c4, 0x4050ecfd, 0x4056b5e4, 0x405c8f86,
    0x406278fe, 0x4068715e, 0x406e77a4, 0x40748ae6, 0x407aaa30, 0x40806a3f,
    0x4083846b, 0x4086a324, 0x4089c5e2, 0x408cec2a, 0x4090157e, 0x40934159,
    0x40966f3e, 0x40999ea5, 0x409ccf12, 0x409ffffe, 0x40a330ea, 0x40a66159,
    0x40a990c0, 0x40acbea0, 0x40afea7e, 0x40b313ce, 0x40b63a16, 0x40b95cd9,
    0x40bc7b8d, 0x40bf95ba, 0x40c2aae5, 0x40c5ba86, 0x40c8c42a, 0x40cbc74e,
    0x40cec37f, 0x40d1b83a, 0x40d4a50c, 0x40d78980, 0x40da651a, 0x40dd376a,
    0x40dfffff, 0x40e2be61, 0x40e57224, 0x40e81ade, 0x40eab81a, 0x40ed4972,
    0x40efce82, 0x40f246de, 0x40f4b21e, 0x40f70fe4, 0x40f95fd3, 0x40fba185,
    0x40fdd4a2, 0x40fff8d4, 0x410106de, 0x41020986, 0x41030438, 0x4103f6ca,
    0x4104e117, 0x4105c2fc, 0x41069c52, 0x41076cf4, 0x410834c8, 0x4108f3a9,
    0x4109a979, 0x410a561c, 0x410af977, 0x410b936f, 0x410c23ec, 0x410caad8,
    0x410d281a, 0x410d9ba2, 0x410e055c, 0x410e6538, 0x410ebb25, 0x410f0717,
    0x410f4901, 0x410f80d8, 0x410fae96, 0x410fd230, 0x410feba2, 0x410ffae8,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
    0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000, 0x41100000,
];

fn density_factor(level: i32) -> f64 {
    match level {
        0..=4 => K_DENSITY_FACTORS[level as usize],
        _ => K_DENSITY_FACTORS[2],
    }
}

/// Dynamic state of one pool star. Defaults mirror the C++ in-class
/// initializers (they are observable: a never-respawned slot keeps them).
#[derive(Clone, Copy)]
struct Star {
    x: f32,
    y: f32,
    base_speed: f32,
    star_type: u8,
    spectral: u8,
    r: u8,
    g: u8,
    b: u8,
    twinkle_phase: u16,
    twinkle_step: u16,
    twinkle_alpha: u8,
}

impl Default for Star {
    fn default() -> Self {
        Self {
            x: 0.0,
            y: 0.0,
            base_speed: 1.0,
            star_type: 0, // StarType::MidField
            spectral: 1,  // SpectralClass::White
            r: 255,
            g: 255,
            b: 255,
            twinkle_phase: 0,
            twinkle_step: 1,
            twinkle_alpha: 255,
        }
    }
}

/// One-star observation sample (golden-vector row payload).
pub struct StarSample {
    pub x: f32,
    pub y: f32,
    pub base_speed: f32,
    pub star_type: u8,
    pub spectral: u8,
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub alpha: u8,
    pub twinkle_phase: u16,
    pub twinkle_step: u16,
}

/// The starfield twin: fixed-capacity pool, private RandomStream and the
/// tracked viewport mirror. The C++ shell polls the live window through
/// CheckWindowResize() (stars_draw.cc); headless consumers -- the dumper and
/// this port's test -- drive SetViewport() instead, which applies the exact
/// same ratio-scale + density-refit sequence.
pub struct StarsFields {
    stars: Vec<Star>,
    active_stars: usize,
    warp_factor: f32,
    warp_timer: i32,
    rng: RandomStream,
    /// Mirror of Config::DetailsLevel() (the kernel has no Config object).
    details_level: i32,
    last_density_level: i32,
    last_window_w: i32,
    last_window_h: i32,
}

impl StarsFields {
    /// Headless construction twin of the C++ `StarsFields(int, int)`:
    /// mirror the viewport, fit the density, spawn the initial pool.
    pub fn new(viewport_w: i32, viewport_h: i32) -> Self {
        let mut sf = Self {
            stars: vec![Star::default(); POOL_CAPACITY],
            active_stars: 0,
            warp_factor: 1.0,
            warp_timer: 0,
            rng: RandomStream::new(STARFIELD_SEED),
            details_level: 2, // C++ reads level 2 while config_ is null
            last_density_level: 2,
            last_window_w: viewport_w.max(1),
            last_window_h: viewport_h.max(1),
        };
        sf.adjust_star_density();
        let n = sf.active_stars;
        for i in 0..n {
            sf.respawn_star(i, true);
        }
        sf
    }

    /// Twin of `SetConfig`: adopt a detail level and refit immediately
    /// (CheckDensityChange inside the next scroll then just re-tags).
    pub fn set_config_details(&mut self, level: i32) {
        self.details_level = level;
        self.adjust_star_density();
    }

    /// Twin of mutating the Config behind the pointer (`AddDetailsLevel`):
    /// the stars notice on the next scroll's CheckDensityChange, exactly
    /// like production.
    pub fn set_details_level(&mut self, level: i32) {
        self.details_level = level;
    }

    /// Explicit viewport change (headless twin of CheckWindowResize): the
    /// mirror updates BEFORE the ratio scale so the density refit reads the
    /// new window -- the same ordering the shell poll enforces.
    pub fn set_viewport(&mut self, viewport_w: i32, viewport_h: i32) {
        let w = viewport_w.max(1);
        let h = viewport_h.max(1);
        if w == self.last_window_w && h == self.last_window_h {
            return;
        }
        let rx = w as f32 / self.last_window_w as f32;
        let ry = h as f32 / self.last_window_h as f32;
        self.last_window_w = w;
        self.last_window_h = h;
        self.on_resize(rx, ry);
    }

    /// Production always triggers with WARP_DURATION_FRAMES (the phase
    /// transition's FastRound derivation); the frozen curve is recorded for
    /// exactly that length, so the port takes no argument.
    pub fn trigger_hyperspace_warp(&mut self) {
        self.warp_timer = WARP_DURATION_FRAMES;
        self.warp_factor = K_WARP_MULT;
    }

    /// One 60 Hz frame of starfield motion: density check, warp easing,
    /// parallax fall + twinkle + edge respawn.
    pub fn scroll(&mut self) {
        self.check_density_change();
        let win_h = self.last_window_h.max(1) as f32;
        if self.warp_timer > 0 {
            self.warp_timer -= 1;
            // Frozen easing: the recorded curve IS the C++ branch (constant
            // multiplier while p < 0.65, cosine blend afterwards) evaluated
            // for every remaining-timer value -- no live transcendental.
            self.warp_factor = f32::from_bits(WARP_CURVE_BITS[self.warp_timer as usize]);
        } else if self.warp_factor > 1.0 {
            // Post-warp settle. Unreachable with the production duration
            // (the curve's last value lands on exactly 1.0f), kept as the
            // faithful twin of the C++ branch.
            self.warp_factor += (1.0 - self.warp_factor) * 0.05;
            if self.warp_factor < 1.02 {
                self.warp_factor = 1.0;
            }
        }
        let max_active = self.active_stars;
        for i in 0..max_active {
            let crossed;
            {
                let s = &mut self.stars[i];
                s.y += s.base_speed * self.warp_factor;
                // C++ casts the u16 sum back to u16: wrapping semantics.
                s.twinkle_phase = s.twinkle_phase.wrapping_add(s.twinkle_step);
                s.twinkle_alpha = TWINKLE_ALPHA[(s.twinkle_phase >> 8) as usize];
                crossed = s.y >= win_h + 15.0;
            }
            if crossed {
                self.respawn_star(i, false);
            }
        }
    }

    pub fn active_stars(&self) -> usize {
        self.active_stars
    }

    pub fn sample_star(&self, i: usize) -> StarSample {
        let s = &self.stars[i];
        StarSample {
            x: s.x,
            y: s.y,
            base_speed: s.base_speed,
            star_type: s.star_type,
            spectral: s.spectral,
            r: s.r,
            g: s.g,
            b: s.b,
            alpha: s.twinkle_alpha,
            twinkle_phase: s.twinkle_phase,
            twinkle_step: s.twinkle_step,
        }
    }

    pub fn warp_timer(&self) -> i32 {
        self.warp_timer
    }

    pub fn warp_factor(&self) -> f32 {
        self.warp_factor
    }

    pub fn viewport_width(&self) -> i32 {
        self.last_window_w
    }

    pub fn viewport_height(&self) -> i32 {
        self.last_window_h
    }

    /// Frozen-lookup observation of the twinkle LUT (corpus `ent.stars.alpha`).
    pub fn twinkle_alpha(index: u8) -> u8 {
        TWINKLE_ALPHA[index as usize]
    }

    /// Pulse01 of the LUT sample (corpus `ent.stars.pulse`): pure arithmetic
    /// on the frozen alpha, so it is recomputed -- not frozen -- here.
    pub fn twinkle_pulse(index: u8) -> f32 {
        let tw = Self::twinkle_alpha(index) as f32 * (1.0 / 255.0);
        let span = K_TWINKLE_CEIL - K_TWINKLE_FLOOR;
        ((tw - K_TWINKLE_FLOOR) / span).clamp(0.0, 1.0)
    }

    fn calculate_target_star_count(&self) -> usize {
        if self.details_level == 0 {
            return 0;
        }
        let w = self.last_window_w.max(640) as f64;
        let h = self.last_window_h.max(480) as f64;
        let base_area = K_BASE_RES_W * K_BASE_RES_H;
        let scale = (w * h / base_area).sqrt();
        let base_target = fast_round_f64(K_BASE_STAR_COUNT * scale) as usize;
        let target = fast_round_f64(base_target as f64 * density_factor(self.details_level)) as usize;
        if target == 0 {
            return 0;
        }
        target.clamp(K_MIN_STARS, K_MAX_STARS * 2)
    }

    fn adjust_star_density(&mut self) {
        // Clamp to the fixed pool capacity; growth respawns (RNG draws, in
        // index order), shrink just lowers the active count.
        let target = self.calculate_target_star_count().min(POOL_CAPACITY);
        if target > self.active_stars {
            for i in self.active_stars..target {
                self.respawn_star(i, true);
            }
        }
        self.active_stars = target;
    }

    fn check_density_change(&mut self) {
        if self.details_level != self.last_density_level {
            self.last_density_level = self.details_level;
            self.adjust_star_density();
        }
    }

    fn on_resize(&mut self, rx: f32, ry: f32) {
        if rx <= 0.0 || ry <= 0.0 {
            return;
        }
        for i in 0..self.active_stars {
            let s = &mut self.stars[i];
            s.x *= rx;
            s.y *= ry;
        }
        self.adjust_star_density();
    }

    fn respawn_star(&mut self, i: usize, random_initial_y: bool) {
        let win_w = self.last_window_w.max(1) as f32;
        let win_h = self.last_window_h.max(1) as f32;
        let s = &mut self.stars[i];
        s.x = self.rng.uniform_float(0.0, win_w);
        // Headless fall respawns park at the fixed -5 sentinel (observable:
        // the dumper counts exactly these for the per-frame respawn row).
        s.y = if random_initial_y {
            self.rng.uniform_float(0.0, win_h)
        } else {
            -5.0
        };

        let roll = self.rng.uniform_float(0.0, 1.0);
        if roll < K_FOREGROUND_THRESHOLD {
            s.star_type = 0; // MidField: distant parallax layer
            s.base_speed = self.rng.uniform_float(K_MID_SPEED_MIN, K_MID_SPEED_MAX);
        } else {
            s.star_type = 1; // Foreground: luminous near layer
            s.base_speed = self.rng.uniform_float(K_FG_SPEED_MIN, K_FG_SPEED_MAX);
        }

        // Fixed-point twinkle: 16-bit phase, top 8 bits address the LUT.
        let phase = self.rng.uniform_float(0.0, K_TAU);
        let speed = self.rng.uniform_float(K_TWINKLE_SPEED_MIN, K_TWINKLE_SPEED_MAX);
        s.twinkle_phase = (phase * K_TWINKLE_PHASE_SCALE / K_TAU) as u32 as u16;
        s.twinkle_step =
            fast_round_f32(speed * K_TWINKLE_PHASE_SCALE / K_TAU).max(1) as u16;

        // OBAFGKM astrophysical spectral classification (draw order is the
        // contract: class roll first, then the class-specific color draws).
        let spec = self.rng.uniform_int(0, 99);
        if spec < 12 {
            s.spectral = 0; // Blue (O/B giants)
            s.r = self.rng.uniform_int(170, 205) as u8;
            s.g = self.rng.uniform_int(215, 240) as u8;
            s.b = 255;
        } else if spec < 40 {
            s.spectral = 1; // White (A/F)
            s.r = self.rng.uniform_int(240, 255) as u8;
            s.g = self.rng.uniform_int(240, 255) as u8;
            s.b = self.rng.uniform_int(245, 255) as u8;
        } else if spec < 75 {
            s.spectral = 2; // Yellow (G, solar-type)
            s.r = 255;
            s.g = self.rng.uniform_int(225, 245) as u8;
            s.b = self.rng.uniform_int(160, 200) as u8;
        } else {
            s.spectral = 3; // OrangeRed (K/M dwarfs)
            s.r = 255;
            s.g = self.rng.uniform_int(140, 185) as u8;
            s.b = self.rng.uniform_int(90, 140) as u8;
        }
    }
}
