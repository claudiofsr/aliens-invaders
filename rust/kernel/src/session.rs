//! Fixed-physics entity pipeline (RUST.md Fatia 5).
//!
//! `CombatSession::TickFixedPhysics` (C++) defines the canonical tick order.
//! This module lifts the ENTITY subset of that order into the kernel: one
//! `EntitySession::tick` drives collisions, economy, player and projectile
//! motion, and the armada in the exact production sequence, so a future Rust
//! shell (Phase 4) consumes one call per fixed 60 Hz frame instead of
//! re-wiring six managers by hand. Everything else the C++ tick does is
//! shell territory -- score context, cinematics, starfield, phase
//! transitions, input devices -- and stays in C++ until then. The session
//! REPORTS kamikaze proximity but does not act on it: in production that
//! branch ends the frame with the supernova cinematic.
//!
//! Golden contract: the 900-frame `ent.fleet.*` corpus was recorded through
//! exactly this pipeline order, and `tests/entity_parity.rs::fleet_parity`
//! replays it via `EntitySession::tick` -- bit-exact on every dynamic column
//! (angle columns carry the documented atan2f ulp budget).

use crate::bonus::BonusManager;
use crate::bullets::BulletsManager;
use crate::explosions::ExplosionsManager;
use crate::fleet::{CollisionSummary, Fleet};
use crate::mathx::Coord;
use crate::player::Player;
use crate::sink::EventSink;

/// Per-frame report of one fixed-physics tick: the collision breakdown the
/// C++ shell reads back from the entity layer (regular/kamikaze hits and the
/// score they earned) plus the kamikaze proximity flag, which in production
/// triggers the supernova cinematic.
pub struct TickReport {
    pub collisions: CollisionSummary,
    pub kamikaze_proximity: bool,
}

/// The entity-layer combat session: the six fixed-physics managers plus the
/// presentation sink, wired in the canonical production order. The consumer
/// builds the managers (seeds and metrics are scenario inputs), hands them
/// over once, and then only drives `tick` and reads state back through the
/// accessors.
pub struct EntitySession {
    fleet: Fleet,
    player: Player,
    bullets: BulletsManager,
    bombs: BulletsManager,
    bonuses: BonusManager,
    explosions: ExplosionsManager,
    sink: EventSink,
    /// Kamikaze blast radius in design pixels: the proximity check flags
    /// "an alien is about to ram the player" within this distance.
    lethal_radius: f32,
    /// Lifetime score earned by the entity layer (sum of the per-frame
    /// `score_earned`); the shell adds its own context on top.
    score_total: u64,
    /// Lifetime alien kills (regular + kamikaze hits).
    kills: i32,
}

impl EntitySession {
    /// Adopts pre-built managers. Construction deliberately stays with the
    /// consumer: seeds, metrics and formation inputs are scenario/shell
    /// inputs, not session policy.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        fleet: Fleet,
        player: Player,
        bullets: BulletsManager,
        bombs: BulletsManager,
        bonuses: BonusManager,
        explosions: ExplosionsManager,
        sink: EventSink,
        lethal_radius: f32,
    ) -> Self {
        Self {
            fleet,
            player,
            bullets,
            bombs,
            bonuses,
            explosions,
            sink,
            lethal_radius,
            score_total: 0,
            kills: 0,
        }
    }

    /// Runs exactly one frame of `CombatSession::TickFixedPhysics` (entity
    /// subset); the order below IS the golden contract. `dir` is the
    /// player's horizontal input (-1/0/+1) and `fire` mirrors the shell's
    /// `input_.Fire()` gate.
    pub fn tick(&mut self, dir: i32, fire: bool) -> TickReport {
        // Every call touches disjoint members, so the borrow checker sees
        // the same sequence the C++ method runs on its member objects.
        let collisions = self.fleet.do_bullets_collisions(
            &mut self.bullets,
            &mut self.explosions,
            &mut self.bonuses,
            &mut self.sink,
            Some(&self.player),
        );
        self.player
            .do_bombs_collisions(&mut self.bombs, &mut self.explosions, &mut self.sink);
        // Production ends the frame here with the supernova cinematic; the
        // kernel only reports -- acting on the flag is the shell's job.
        let kamikaze_proximity = self
            .fleet
            .check_kamikaze_proximity(self.player.position(), self.lethal_radius);
        self.player.do_bonus_collisions(
            &mut self.bonuses,
            &mut self.explosions,
            &mut self.sink,
            Some(&mut self.fleet),
        );
        self.bonuses.advance();
        self.player.move_player(dir);
        // Player bullets travel upward in screen space: (-1, -1) per frame.
        self.bullets.advance(Coord::new(-1, -1));
        let player_pos = self.player.position();
        self.bombs.advance(player_pos);
        if fire {
            self.player.fire(&mut self.bullets, &mut self.sink);
        }
        self.explosions.advance();
        self.fleet
            .advance(&self.bullets, &mut self.sink, Some(player_pos));
        self.fleet.fire(player_pos, &mut self.bombs);

        self.score_total += collisions.score_earned;
        self.kills += collisions.total_hits;
        TickReport { collisions, kamikaze_proximity }
    }

    pub fn fleet(&self) -> &Fleet {
        &self.fleet
    }

    pub fn player(&self) -> &Player {
        &self.player
    }

    pub fn bullets(&self) -> &BulletsManager {
        &self.bullets
    }

    pub fn bombs(&self) -> &BulletsManager {
        &self.bombs
    }

    pub fn bonuses(&self) -> &BonusManager {
        &self.bonuses
    }

    pub fn explosions(&self) -> &ExplosionsManager {
        &self.explosions
    }

    /// Lifetime score earned inside the entity layer.
    pub fn score_total(&self) -> u64 {
        self.score_total
    }

    /// Lifetime alien kills (regular + kamikaze).
    pub fn kills(&self) -> i32 {
        self.kills
    }

    /// The sink records by design: the consumer drains it every frame (the
    /// replay harness drains into corpus rows; the Phase-4 shell will drain
    /// into presentation).
    pub fn sink_mut(&mut self) -> &mut EventSink {
        &mut self.sink
    }
}
