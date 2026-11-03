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
//! Since Fatia 10 the session also owns the phase transition: the trigger
//! on a finished armada (the fanfare duration is a scripted INPUT -- in
//! production it comes from the baked WAV through `PlayStageFanfare`,
//! device-dependent at exactly 6.0 s +/-1 sample), the hold countdown that
//! freezes the armada, the `Score::IncLevel`, and the rebuild that consumes
//! the Fatia 9 catalog (`level_data`) with one `RandomStream` draw per wave
//! -- the production `ctx_.rng` role. Everything else the C++ tick does is
//! shell territory -- cinematics, starfield scroll, the warp trigger itself,
//! input devices -- and stays in C++ until then. The session REPORTS
//! kamikaze proximity and player destruction but does not act on them: in
//! production those branches end the frame with cinematics.
//!
//! Golden contract: the 900-frame `ent.fleet.*` corpus was recorded through
//! exactly this pipeline order, and `tests/entity_parity.rs::fleet_parity`
//! replays it via `EntitySession::tick` -- bit-exact on every dynamic column
//! (angle columns carry the documented atan2f ulp budget).

use crate::bonus::BonusManager;
use crate::bullets::BulletsManager;
use crate::explosions::ExplosionsManager;
use crate::fleet::{CollisionSummary, Fleet};
use crate::level_data;
use crate::mathx::Coord;
use crate::player::Player;
use crate::random_stream::RandomStream;
use crate::score::Score;
use crate::sink::EventSink;
use crate::stage_catalog;
use crate::stars::K_SIM_HZ;
use crate::trajectory::ViewportMetrics;

/// Per-frame report of one fixed-physics tick: the collision breakdown the
/// C++ shell reads back from the entity layer (regular/kamikaze hits and the
/// score they earned), the kamikaze proximity flag, which in production
/// triggers the supernova cinematic, and the number of extra-shield
/// milestones this frame awarded (the shell plays one SFX per award).
/// Fatia 10 adds the phase-transition columns: the shell fires the fanfare
/// and the hyperspace warp on `transition_started`, reads the overlay
/// progress from `transition_frames`/`transition_remaining`, and plays the
/// destruction cinematic on `player_destroyed` -- the kernel reports, the
/// shell presents.
pub struct TickReport {
    pub collisions: CollisionSummary,
    pub kamikaze_proximity: bool,
    pub extra_shields: u64,
    /// `player.shield() < 0` after the per-frame score sync: production ends
    /// the frame with the grand-destruction cinematic; the kernel reports and
    /// keeps ticking (the kamikaze-proximity precedent).
    pub player_destroyed: bool,
    /// The armada finished THIS frame: the shell plays the stage fanfare and
    /// triggers the starfield warp (`warp_triggered` mirrors the production
    /// `TriggerHyperspaceWarp` call the shell owns).
    pub transition_started: bool,
    /// Hold set on the trigger frame (`phase_transition_frames` before the
    /// first decrement); 0 on every other frame. The shell latches it as the
    /// overlay-progress denominator (`total_transition_frames_`).
    pub transition_frames: i32,
    /// Countdown after this frame's decrement; 0 when no hold is active.
    pub transition_remaining: i32,
    /// Starfield hyperspace warp trigger for this frame (360 frames in
    /// production -- `stars::WARP_DURATION_FRAMES`).
    pub warp_triggered: bool,
    /// The countdown hit zero THIS frame and the armada was rebuilt from the
    /// catalog at `rebuild_level` with the `rebuild_seed` drawn from the
    /// session stream.
    pub rebuilt: bool,
    pub rebuild_level: i32,
    pub rebuild_seed: u32,
}

/// The phase-transition hold formula (Fatia 10, `pt.hold` corpus rows):
/// `hold = max(6.0 s, fanfare duration)`, counted at the 60 Hz simulation
/// rate, never shorter than one frame. The bake-edge durations (6.0 s + one
/// sample at 44.1/48 kHz) push the hold to 361 frames -- the corpus pins
/// every bit of that ceil.
pub fn phase_transition_frames(duration_seconds: f32) -> i32 {
    let hold = stage_catalog::K_STAGE_FANFARE_DURATION_SECONDS.max(duration_seconds);
    ((hold * K_SIM_HZ).ceil() as i32).max(1)
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
    /// Viewport metrics for catalog rebuilds (Fatia 10): `Fleet::new` needs
    /// them and the session outlives any single wave.
    metrics: ViewportMetrics,
    /// The production `ctx_.rng` role: one `next_u32` per rebuild seeds the
    /// new wave.
    rng: RandomStream,
    /// Stage-fanfare durations in seconds, indexed by `wave_in_stage` -- the
    /// kernel contract takes them as INPUT (production reads them from the
    /// baked WAVs, which are device-dependent at +/-1 sample).
    fanfare_durations: [f32; stage_catalog::WAVES_PER_STAGE as usize],
    /// Phase-transition countdown (Fatia 10): `phase_transition_frames_` and
    /// `phase_transition_started_` in production. The overlay denominator
    /// (`total_transition_frames_`) is shell-side -- it equals the hold set
    /// on the trigger frame, which `TickReport::transition_frames` reports.
    phase_frames: i32,
    phase_started: bool,
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
        metrics: ViewportMetrics,
        rebuild_seed: u32,
        fanfare_durations: [f32; stage_catalog::WAVES_PER_STAGE as usize],
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
            metrics,
            rng: RandomStream::new(rebuild_seed),
            fanfare_durations,
            phase_frames: 0,
            phase_started: false,
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
        // Production syncs the score's shield mirror here (the bonus-mask
        // economy reads it) and ends the frame with the destruction cinematic
        // when the shield went negative; the kernel reports and continues.
        self.score.set_shield(self.player.shield());
        let player_destroyed = self.player.shield() < 0;

        // Phase-transition trigger: a finished armada starts the fanfare hold.
        // The duration is the scripted input for the wave being completed
        // (`wave_in_stage`), the hold formula is `phase_transition_frames`,
        // and `IncLevel` lands BEFORE the countdown so the rebuild frame
        // reads the NEXT level -- the production order, bit for bit.
        let mut transition_started = false;
        let mut transition_frames = 0;
        if !self.phase_started && self.fleet.finished() {
            let completed = self.score.level();
            let slot = stage_catalog::wave_in_stage(completed).max(0) as usize;
            let frames = phase_transition_frames(self.fanfare_durations[slot]);
            self.phase_frames = frames;
            self.phase_started = true;
            self.score.inc_level();
            transition_started = true;
            transition_frames = frames;
        }

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

        // During the hold the armada is frozen (production skips its
        // Move/Fire); when the countdown hits zero the wave is rebuilt from
        // the Fatia 9 catalog -- `Fleet::new` is the documented bit-twin of
        // the production `Rebuild` for the same seed, and the rebuild frame
        // itself still skips Move/Fire (the early return of the C++ block).
        let mut rebuilt = false;
        let mut rebuild_level = 0;
        let mut rebuild_seed = 0u32;
        let held = self.phase_started;
        if self.phase_started {
            self.phase_frames -= 1;
            if self.phase_frames <= 0 {
                let level = self.score.level();
                rebuild_seed = self.rng.next_u32();
                self.fleet = Fleet::new(
                    level,
                    level_data::get_convoy_data(level as usize).to_vec(),
                    level_data::paths().to_vec(),
                    self.metrics,
                    level_data::get_total_levels() as i32,
                    rebuild_seed,
                );
                self.phase_started = false;
                rebuilt = true;
                rebuild_level = level;
            }
        } else {
            self.fleet
                .advance(&self.bullets, &mut self.sink, Some(player_pos));
            self.fleet.fire(player_pos, &mut self.bombs);
        }
        let transition_remaining = if held { self.phase_frames } else { 0 };

        self.kills += collisions.total_hits;
        TickReport {
            collisions,
            kamikaze_proximity,
            extra_shields,
            player_destroyed,
            transition_started,
            transition_frames,
            transition_remaining,
            // The warp fires with the trigger (production calls
            // TriggerHyperspaceWarp inside the same block); the starfield
            // itself is the consumer's -- the report is the shell's cue.
            warp_triggered: transition_started,
            rebuilt,
            rebuild_level,
            rebuild_seed,
        }
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
