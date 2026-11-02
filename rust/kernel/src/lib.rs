//! Pure simulation kernel: the Rust twin of the C++ headers (RUST.md Phase 2).
//! Bit-exactness against the C++ is enforced by `tests/golden.rs`.

pub mod alien;
pub mod bonus;
pub mod bullets;
pub mod dist;
pub mod explosions;
pub mod fleet;
pub mod formation;
pub mod game_object;
pub mod highscore;
pub mod mathx;
pub mod pacing;
pub mod path;
pub mod player;
pub mod random_stream;
pub mod score_hash;
pub mod session;
pub mod sink;
pub mod stage_catalog;
pub mod stars;
pub mod textures;
pub mod trajectory;

