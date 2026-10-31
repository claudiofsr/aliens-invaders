//! Pure simulation kernel: the Rust twin of the C++ headers (RUST.md Phase 2).
//! Bit-exactness against the C++ is enforced by `tests/golden.rs`.

pub mod formation;
pub mod highscore;
pub mod mathx;
pub mod pacing;
pub mod path;
pub mod random_stream;
pub mod score_hash;
pub mod stage_catalog;
pub mod trajectory;
