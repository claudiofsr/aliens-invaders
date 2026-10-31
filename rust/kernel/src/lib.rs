//! Pure simulation kernel: the Rust twin of the C++ headers (RUST.md Phase 2).
//! Bit-exactness against the C++ is enforced by `tests/golden.rs`.

pub mod formation;
pub mod mathx;
pub mod pacing;
pub mod random_stream;
