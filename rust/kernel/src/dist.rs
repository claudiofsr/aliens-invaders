//! libstdc++-compatible draw shims (RUST.md slice 4b).
//!
//! The C++ entity layer feeds `RandomStream` (a URNG with a full 2^32 output
//! range) to `std::uniform_int_distribution` and `std::shuffle`. Both are
//! IMPLEMENTATION-DEFINED: the golden corpus therefore freezes one concrete
//! algorithm, and this module is its twin.
//!
//! Pinned behavior -- libstdc++ as shipped since GCC 11 (r11-3757): the
//! distribution detects the power-of-two engine range and downscales with
//! Lemire's nearly divisionless method (`_S_nd<uint64>(g, u32 range)`), and
//! `std::shuffle` takes the paired fast path (`__gen_two_uniform_ints`)
//! whenever `urngrange / n >= n` -- always true for the small populations
//! here. Toolchains older than GCC 11 (division-rejection downscaling) or a
//! future libstdc++ that changes either algorithm would regenerate different
//! corpus rows; the parity tests then fail loudly instead of drifting.

use crate::random_stream::RandomStream;

/// Twin of `std::uniform_int_distribution<int>{a, b}(rng)` (and of the
/// `uint64` instantiation `std::shuffle` uses -- both funnel into the same
/// 32-bit Lemire downscale because the engine range is exactly `u32::MAX`).
pub fn uniform_int(rng: &mut RandomStream, a: i32, b: i32) -> i32 {
    // __urange / __uerange in wrapping u32 arithmetic: negative bounds (e.g.
    // the deflection jitter uid(-2, 2)) map through the same bit patterns the
    // C++ two's-complement subtraction produces.
    let uerange = (b as u32).wrapping_sub(a as u32).wrapping_add(1);
    let mut product = u64::from(rng.next_u32()) * u64::from(uerange);
    let mut low = product as u32;
    if low < uerange {
        // threshold = (-range) % range in u32: the Lemire rejection bound.
        let threshold = uerange.wrapping_neg() % uerange;
        while low < threshold {
            product = u64::from(rng.next_u32()) * u64::from(uerange);
            low = product as u32;
        }
    }
    let ret = (product >> 32) as u32;
    (ret.wrapping_add(a as u32)) as i32
}

/// Twin of `std::shuffle(first, first + n, rng)` over the index vector
/// `0..n`, returning the permuted indices. libstdc++ swaps ascending in
/// pairs: an even n draws one uid{0,1} up front, then one combined
/// uid{0, b0*b1-1} per two positions (x / b1 and x % b1).
pub fn shuffle_indices(rng: &mut RandomStream, n: usize) -> Vec<usize> {
    let mut v: Vec<usize> = (0..n).collect();
    if n <= 1 {
        return v;
    }
    let mut i = 1usize;
    if n % 2 == 0 {
        let d = uniform_int(rng, 0, 1) as usize;
        v.swap(1, d);
        i = 2;
    }
    while i < n {
        // __gen_two_uniform_ints(b0 = i+1, b1 = i+2): one draw over the
        // composed range, split by division/modulus into the two swap slots.
        let b0 = (i + 1) as i64;
        let b1 = b0 + 1;
        let x = uniform_int(rng, 0, (b0 * b1 - 1) as i32) as i64;
        let p1 = (x / b1) as usize;
        let p2 = (x % b1) as usize;
        v.swap(i, p1);
        v.swap(i + 1, p2);
        i += 2;
    }
    v
}
