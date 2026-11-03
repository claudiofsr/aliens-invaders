//! Fixed-physics entity pipeline (RUST.md Fatia 5).
//!
//! `CombatSession::TickFixedPhysics` (C++) defines the canonical tick order.
//! This module lifts the ENTITY subset of that order into the kernel: one
//! `EntitySession::tick` drives collisions, economy, player and projectile
//! motion, and the armada in the exact production sequence, so a future Rust
//! shell (Phase 4) consumes one call per fixed 60 Hz frame instead of
//! re-wiring six managers by hand. Since Fatia 8 the session also owns the
//! score context: the production block between the bullet and the bomb
//! collisions (credit `score_earned`, drain the extra-shield milestones,
//! apply each award to the player) runs inside `tick`, and the milestone
//! count rides back on `TickReport` -- the C++ shell plays one
//! SFX_SHIELD_RESTORE per milestone, and presentation stays shell-side.
//! Everything else the C++ tick does is shell territory -- cinematics,
//! starfield scroll, phase transitions, input devices -- and stays in C++
//! until then. The session REPORTS kamikaze proximity but does not act on
//! it: in production that branch ends the frame with the supernova
//! cinematic.
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
use crate::score::Score;
use crate::sink::EventSink;

/// Per-frame report of one fixed-physics tick: the collision breakdown the
/// C++ shell reads back from the entity layer (regular/kamikaze hits and the
/// score they earned), the kamikaze proximity flag, which in production
/// triggers the supernova cinematic, and the number of extra-shield
/// milestones this frame awarded (the shell plays one SFX per award).
pub struct TickReport {
    pub collisions: CollisionSummary,
    pub kamikaze_proximity: bool,
    pub extra_shields: u64,
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
    /// The session's score economy (Fatia 8): receives the per-frame
    /// `score_earned`, owns the extra-shield milestone ledger and drives the
    /// player's `ExtraShield` awards -- the production `ctx_.score` role.
    score: Score,
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
            score: Score::new(),
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
        // The production score-context block (TickFixedPhysics): credit the
        // frame's earnings, drain the extra-shield milestones and apply each
        // award to the player -- exactly between the two collision passes.
        let extra_shields = self.apply_score_context(collisions.score_earned);
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

        self.kills += collisions.total_hits;
        TickReport { collisions, kamikaze_proximity, extra_shields }
    }

    /// The production score-context block, factored so the golden epilogue
    /// can feed it synthetic amounts: credit (only when non-zero, mirroring
    /// the C++ `if (hit_summary.score_earned > 0)` guard), drain the
    /// milestone ledger and apply one `ExtraShield` per milestone. Returns
    /// the number of shields awarded by this call.
    fn apply_score_context(&mut self, earned: u64) -> u64 {
        if earned > 0 {
            self.score.add(earned);
        }
        let milestones = self.score.check_extra_shield_milestones();
        for _ in 0..milestones {
            self.player.extra_shield();
        }
        milestones
    }

    /// Post-replay award entry point for the golden harness (the
    /// `ent.fs.award` rows): the same production block driven by scripted
    /// amounts after the fleet scenario ends.
    pub fn award_points(&mut self, pts: u64) -> u64 {
        self.apply_score_context(pts)
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

    /// The session's score economy (points, milestone ledger, level, bonus
    /// mask): the kernel twin of the shell's `ctx_.score`.
    pub fn score(&self) -> &Score {
        &self.score
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
