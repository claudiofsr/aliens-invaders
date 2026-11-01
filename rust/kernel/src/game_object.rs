//! Port of src/simulation/game_object.h restricted to what the simulation
//! tick reads: position, scale-adjusted hitbox dimensions. Rendering-side
//! members (Motion integration, Renderable pixmap pointer, AABB component)
//! are shell territory -- the entity corpus never observes them, and the
//! collision code paths use the integer hitbox math ported in bullets.rs.

use crate::mathx::{fast_round_f32, Coord};

pub struct GameObject {
    pub pos: Coord,
    /// Base pixmap dimensions (headless texture contract, see textures.rs).
    pub pix_w: i32,
    pub pix_h: i32,
    /// Alien4 alpha decay scales the hull (and its hitbox) to 0.5.
    pub scale: f32,
}

impl GameObject {
    pub fn new(pos: Coord, pix_w: i32, pix_h: i32) -> Self {
        Self { pos, pix_w, pix_h, scale: 1.0 }
    }

    /// GameObject::Width(): the scale shortcut keeps scale-1 hitboxes free of
    /// rounding, exactly like the C++ (matters for bit-parity of FastRound).
    pub fn width(&self) -> i32 {
        if self.scale == 1.0 {
            self.pix_w
        } else {
            fast_round_f32(self.pix_w as f32 * self.scale)
        }
    }

    pub fn height(&self) -> i32 {
        if self.scale == 1.0 {
            self.pix_h
        } else {
            fast_round_f32(self.pix_h as f32 * self.scale)
        }
    }
}

// ---- Motion helpers (port of src/simulation/motion.h) ----------------------
// Pure kinematics primitives shared by the Alien warp transit, the Delta Jump
// Guard and the angular partitioning. No hidden state: every function is a
// literal twin of the C++ inline, operation order included.

use crate::path::Vec2f;

/// Twin of simulation::AdvanceTurnDeg: one rate-limited ease-out turn slice
/// along the shortest arc -- proportional when far, at least `snap_deg`,
/// never more than `cap_deg` per 60 Hz tick.
pub fn advance_turn_deg(
    angle_deg: f32,
    target_deg: f32,
    response: f32,
    snap_deg: f32,
    cap_deg: f32,
) -> f32 {
    let mut diff = target_deg - angle_deg;
    while diff < -180.0 {
        diff += 360.0;
    }
    while diff > 180.0 {
        diff -= 360.0;
    }
    let step = (diff.abs() * response).clamp(snap_deg, cap_deg);
    let mut next = angle_deg
        + if diff.abs() <= step {
            diff
        } else {
            step.copysign(diff)
        };
    while next > 180.0 {
        next -= 360.0;
    }
    while next < -180.0 {
        next += 360.0;
    }
    next
}

/// Twin of simulation::ClampStepToBudget (Delta Jump Guard arithmetic):
/// rescales the step so no axis exceeds `budget` (Chebyshev clamp, direction
/// preserved); a non-positive budget freezes the position.
pub fn clamp_step_to_budget(from: Vec2f, to: Vec2f, budget: f32) -> Vec2f {
    if budget <= 0.0 {
        return from;
    }
    let jump = to.sub(from);
    let max_axis = jump.x.abs().max(jump.y.abs());
    if max_axis <= budget {
        return to;
    }
    from.add(jump.mul(budget / max_axis))
}

/// Twin of simulation::AdvanceToward: pursuit primitive that lands EXACTLY on
/// the destination inside one step, so a chase can never orbit or overshoot.
pub fn advance_toward(from: Vec2f, to: Vec2f, step: f32) -> Vec2f {
    let delta = to.sub(from);
    let dist = delta.length();
    if dist <= step || dist <= 0.0 {
        return to;
    }
    from.add(delta.mul(step / dist))
}
