//! Ports of the pure math helpers of src/core/math_types.h. Same spellings,
//! same edge behavior (golden vectors pin every branch).

/// Round-half-away-from-zero, exactly like the C++ FastRound(float).
pub fn fast_round_f32(v: f32) -> i32 {
    if v >= 0.0 {
        (v + 0.5) as i32
    } else {
        (v - 0.5) as i32
    }
}

/// Double variant of FastRound (C++ truncates toward zero on the cast).
pub fn fast_round_f64(v: f64) -> i32 {
    if v >= 0.0 {
        (v + 0.5) as i32
    } else {
        (v - 0.5) as i32
    }
}

/// Integer division floored toward negative infinity (C++ FloorDiv).
pub fn floor_div(numerator: i32, denominator: i32) -> i32 {
    if denominator == 0 {
        return 0;
    }
    let quotient = numerator / denominator;
    let remainder = numerator % denominator;
    if remainder != 0 && ((numerator < 0) != (denominator < 0)) {
        quotient - 1
    } else {
        quotient
    }
}

/// Chebyshev distance (L-infinity) over integer cells -- no sqrt, no abs call.
pub fn chebyshev(x1: i32, y1: i32, x2: i32, y2: i32) -> i32 {
    let dx = if x1 > x2 { x1 - x2 } else { x2 - x1 };
    let dy = if y1 > y2 { y1 - y2 } else { y2 - y1 };
    if dx > dy {
        dx
    } else {
        dy
    }
}
