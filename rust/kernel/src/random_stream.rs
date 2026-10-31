//! Deterministic zero-allocation PRNG: port of src/core/random_stream.h.
//! Draw order and rejection logic must match the C++ exactly (golden vectors).

pub struct RandomStream {
    state: u32,
}

impl RandomStream {
    /// Non-zero remap used by the C++ whenever a seed/state collapses to zero.
    pub const FALLBACK_SEED: u32 = 0xA341_316C;

    pub fn new(seed: u32) -> Self {
        Self {
            state: if seed == 0 { Self::FALLBACK_SEED } else { seed },
        }
    }

    pub fn reseed(&mut self, seed: u32) {
        self.state = if seed == 0 { Self::FALLBACK_SEED } else { seed };
    }

    pub fn next_u32(&mut self) -> u32 {
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        self.state = if x == 0 { Self::FALLBACK_SEED } else { x };
        self.state
    }

    /// Rejection-bounded uniform integer in [min, max] (inclusive, unbiased).
    pub fn uniform_int(&mut self, min_value: i32, max_value: i32) -> i32 {
        let (lo, hi) = if min_value > max_value {
            (max_value, min_value)
        } else {
            (min_value, max_value)
        };
        let span = (i64::from(hi) - i64::from(lo)) as u64 + 1;
        let limit = (u64::from(u32::MAX) + 1) / span * span;
        let value = loop {
            let v = self.next_u32();
            if u64::from(v) < limit {
                break v;
            }
        };
        lo + ((u64::from(value) % span) as i32)
    }

    pub fn uniform_float(&mut self, min_value: f32, max_value: f32) -> f32 {
        let (lo, hi) = if min_value > max_value {
            (max_value, min_value)
        } else {
            (min_value, max_value)
        };
        const INV_U32: f32 = 1.0f32 / 4294967295.0f32;
        let unit = self.next_u32() as f32 * INV_U32;
        lo + (hi - lo) * unit
    }

    pub fn chance(&mut self, numerator: i32, denominator: i32) -> bool {
        if denominator <= 0 || numerator <= 0 {
            return false;
        }
        if numerator >= denominator {
            return true;
        }
        self.uniform_int(1, denominator) <= numerator
    }

    pub fn bernoulli(&mut self, probability: f32) -> bool {
        if probability <= 0.0 {
            return false;
        }
        if probability >= 1.0 {
            return true;
        }
        self.uniform_float(0.0, 1.0) < probability
    }
}
