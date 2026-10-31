//! Port of src/core/path.h: Barry-Goldman centripetal Catmull-Rom (alpha 0.5)
//! plus the 3.0-unit arc-length resampler. Uses only sqrt and IEEE arithmetic,
//! so the golden vectors compare this module BIT-EXACT across platforms.

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Vec2f {
    pub x: f32,
    pub y: f32,
}

impl Vec2f {
    pub fn new(x: f32, y: f32) -> Self {
        Self { x, y }
    }
    pub fn sub(self, o: Self) -> Self {
        Self::new(self.x - o.x, self.y - o.y)
    }
    pub fn add(self, o: Self) -> Self {
        Self::new(self.x + o.x, self.y + o.y)
    }
    pub fn mul(self, s: f32) -> Self {
        Self::new(self.x * s, self.y * s)
    }
    pub fn length_squared(self) -> f32 {
        self.x * self.x + self.y * self.y
    }
    pub fn length(self) -> f32 {
        self.length_squared().sqrt()
    }
}

const FINE_STEPS: i32 = 36;
const TARGET_STEP: f32 = 3.0;

/// One Barry-Goldman pyramid evaluation between p1 and p2 at t in [0, 1).
pub fn evaluate_centripetal_cr(p0: Vec2f, p1: Vec2f, p2: Vec2f, p3: Vec2f, t: f32) -> Vec2f {
    fn knot(a: Vec2f, b: Vec2f) -> f32 {
        let d2 = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
        d2.sqrt().max(1e-4).sqrt()
    }
    fn interp(a: Vec2f, b: Vec2f, ta: f32, tb: f32, cur: f32) -> Vec2f {
        if (tb - ta).abs() < 1e-5 {
            return a;
        }
        let factor = (cur - ta) / (tb - ta);
        Vec2f::new(a.x + (b.x - a.x) * factor, a.y + (b.y - a.y) * factor)
    }

    let t0 = 0.0f32;
    let t1 = t0 + knot(p0, p1);
    let t2 = t1 + knot(p1, p2);
    let t3 = t2 + knot(p2, p3);
    let time = t1 + t * (t2 - t1);

    let a1 = interp(p0, p1, t0, t1, time);
    let a2 = interp(p1, p2, t1, t2, time);
    let a3 = interp(p2, p3, t2, t3, time);
    let b1 = interp(a1, a2, t0, t2, time);
    let b2 = interp(a2, a3, t1, t3, time);
    interp(b1, b2, t1, t2, time)
}

/// Twin of GenerateSmoothSplineInto/F: 36 subdivisions per segment, then
/// equidistant 3.0-unit resampling with the same final-point snap rule.
pub fn generate_smooth_spline(controls: &[Vec2f]) -> Vec<Vec2f> {
    let n = controls.len();
    if n < 2 {
        return controls.to_vec();
    }

    let mut fine: Vec<Vec2f> = Vec::with_capacity((n - 1) * FINE_STEPS as usize + 2);
    for i in 0..n - 1 {
        let p1 = controls[i];
        let p2 = controls[i + 1];
        let p0 = if i == 0 { p1 } else { controls[i - 1] };
        let p3 = if i + 2 < n { controls[i + 2] } else { p2 };
        for s in 0..FINE_STEPS {
            let t = s as f32 / FINE_STEPS as f32;
            fine.push(evaluate_centripetal_cr(p0, p1, p2, p3, t));
        }
    }
    fine.push(controls[n - 1]);

    let mut out: Vec<Vec2f> = Vec::with_capacity(fine.len() + 16);
    out.push(fine[0]);
    let mut accumulated = 0.0f32;
    for i in 1..fine.len() {
        let p_prev = fine[i - 1];
        let p_curr = fine[i];
        let seg_len = p_curr.sub(p_prev).length();
        if seg_len <= 1e-4 {
            continue;
        }
        accumulated += seg_len;
        while accumulated >= TARGET_STEP {
            let overshoot = accumulated - TARGET_STEP;
            let alpha = ((seg_len - overshoot) / seg_len).clamp(0.0, 1.0);
            out.push(p_prev.add(p_curr.sub(p_prev).mul(alpha)));
            accumulated -= TARGET_STEP;
        }
    }
    let last = fine[fine.len() - 1];
    if out[out.len() - 1].sub(last).length() > 0.5 {
        out.push(last);
    }
    out
}
