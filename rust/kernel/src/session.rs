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
//! Fatia 12 adds the MATCH FLOW: the production responses to those two
//! reports, the cheat-input block, the final-score commit ledger and the
//! timing/alpha math the end-of-run cinematics are cut from. The tick splits
//! into its two production halves -- `tick_collisions` (everything up to the
//! shield sync, INCLUDING the production early return on kamikaze proximity)
//! and `tick_motion` (the transition trigger, the advances and the rebuild)
//! -- so a consumer can end the frame between them exactly where
//! TickFixedPhysics does. `tick` stays the report-only composition the Fatia
//! 5-11 corpora were recorded through: it runs both halves unconditionally
//! and reports the flags instead of acting on them. The match-flow methods
//! (`kamikaze_supernova_response`, `grand_destruction_response`,
//! `apply_cheat`, `commit_final_score`, the cinematic ticks and the debrief
//! predicates) mirror the CombatSession branch bodies minus their Gfx/audio/
//! input calls, and the free functions pin the derived frame counts and the
//! transition-overlay alpha curve.
//!
//! Golden contract: the 900-frame `ent.fleet.*` corpus was recorded through
//! exactly this pipeline order, and `tests/entity_parity.rs::fleet_parity`
//! replays it via `EntitySession::tick` -- bit-exact on every dynamic column
//! (angle columns carry the documented atan2f ulp budget). The `mf.*`,
//! `hs.*` and `ent.mf.*` corpora (Fatia 12) pin the match-flow halves via
//! `match_flow_parity`.

use crate::bonus::BonusManager;
use crate::bullets::BulletsManager;
use crate::explosions::ExplosionsManager;
use crate::fleet::{CollisionSummary, Fleet};
use crate::formation;
use crate::highscore::HighScores;
use crate::level_data;
use crate::mathx::Coord;
use crate::player::Player;
use crate::random_stream::RandomStream;
use crate::score::Score;
use crate::sink::EventSink;
use crate::stage_catalog;
use crate::stars::K_SIM_HZ;
use crate::trajectory::ViewportMetrics;

// ---- match-flow policy constants (CombatSession, golden-vector policed) ----

/// The supernova cinematic runs 3.0 s of refresh-rate frames
/// (`ExecuteKamikazeSupernova`).
pub const NOVA_CINEMATIC_SECONDS: f64 = 3.0;
/// The destruction cinematic runs 1.35 s of refresh-rate frames
/// (`ExecuteGrandPlayerDestruction`).
pub const DESTRUCTION_CINEMATIC_SECONDS: f64 = 1.35;
/// The debriefing game-over banner holds for 6.0 s of refresh-rate frames.
pub const GAME_OVER_SECONDS: f64 = 6.0;
/// The particle-details OSD stays lit for 1.5 s of refresh-rate frames.
pub const DETAILS_OSD_SECONDS: f64 = 1.5;
/// The supernova cinematic becomes skippable after 1/6 of its frames.
pub const NOVA_SKIP_DIVISOR: i32 = 6;
/// The destruction cinematic becomes skippable after 1/4 of its frames.
pub const DESTRUCTION_SKIP_DIVISOR: i32 = 4;
/// GameRules::Fleet::kKamikazeBlastRadiusMultiplier (golden-vector policed
/// copy): the lethal proximity radius is the formation grid width times this.
pub const KAMIKAZE_BLAST_RADIUS_MULTIPLIER: f32 = 1.4;

// The transition-overlay alpha knees (CombatSession::Run): ramp in over the
// first 18% of the hold, full between the knees, ramp out over the last 22%.
const OVERLAY_RAMP_IN_END: f32 = 0.18;
const OVERLAY_RAMP_OUT_START: f32 = 0.78;
const OVERLAY_RAMP_OUT_SPAN: f32 = 0.22;

/// A cinematic length in frames: the C++ `static_cast<int>(RefreshRate() *
/// seconds)` -- an IEEE-754 double multiply, then truncation toward zero.
/// The mf.cinematic corpus rows pin the truncation at eight refresh rates
/// (e.g. 1.35 s at 75 Hz is 101 frames, not 102).
pub fn cinematic_frames(refresh_rate: i32, seconds: f64) -> i32 {
    (f64::from(refresh_rate) * seconds) as i32
}

/// The details-OSD lifetime in frames (`input_.Details()` response).
pub fn details_osd_frames(refresh_rate: i32) -> i32 {
    cinematic_frames(refresh_rate, DETAILS_OSD_SECONDS)
}

/// The transition-overlay alpha at `progress` in [0, 1], lifted from the
/// CombatSession::Run curve: linear ramp in over the first 18%, 1.0 between
/// the knees, linear ramp out over the last 22% clamped at zero. The
/// mf.overlay corpus rows pin every branch bit-exactly (f32 arithmetic).
pub fn overlay_alpha_at(progress: f32) -> f32 {
    if progress < OVERLAY_RAMP_IN_END {
        return progress / OVERLAY_RAMP_IN_END;
    }
    if progress > OVERLAY_RAMP_OUT_START {
        return ((1.0 - progress) / OVERLAY_RAMP_OUT_SPAN).max(0.0);
    }
    1.0
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

/// The collision half of one fixed-physics frame (`tick_collisions`): the
/// bullet/bomb passes with the score context between them, the kamikaze
/// proximity probe, and the shield sync with its destruction predicate.
/// `bomb_shield_loss` mirrors the production rumble gate (`Shield() <
/// old_shield` across the bomb pass) and rides the ent.mf.death.frame rows.
pub struct CollisionReport {
    pub collisions: CollisionSummary,
    pub extra_shields: u64,
    pub bomb_shield_loss: bool,
    pub kamikaze_proximity: bool,
    /// `player.shield() < 0` after the per-frame score sync.
    pub player_destroyed: bool,
}

/// The motion half of one fixed-physics frame (`tick_motion`): the phase
/// transition trigger/countdown/rebuild and every manager advance. Field
/// semantics are the Fatia 10 `TickReport` ones.
pub struct MotionReport {
    pub transition_started: bool,
    pub transition_frames: i32,
    pub transition_remaining: i32,
    pub warp_triggered: bool,
    pub rebuilt: bool,
    pub rebuild_level: i32,
    pub rebuild_seed: u32,
}

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
    /// The bomb pass cost shield THIS frame (the production rumble gate).
    pub bomb_shield_loss: bool,
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

/// The entity-layer combat session: the six fixed-physics managers plus the
/// presentation sink, wired in the canonical production order. The consumer
/// builds the managers (seeds and metrics are scenario inputs), hands them
/// over once, and then only drives `tick` and reads state back through the
/// accessors.
/// Outcome of `apply_resolution_change` -- the four exits of the production
/// guard chain (CombatSession::ApplyResolutionChange). The parity test
/// rebuilds the ent.rz.resize rows from this plus the post-cascade state.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ResizeOutcome {
    /// Non-positive size: production returns without touching anything.
    Invalid,
    /// First call: the window size is latched WITHOUT scaling (lazy init).
    Latched,
    /// Same size as the applied one: early return, zero state change.
    NoOp,
    /// Full cascade applied; carries the cumulative float ratios computed
    /// from the previously applied size, and that previous size.
    Applied {
        rx: f32,
        ry: f32,
        prev_applied: (i32, i32),
    },
}

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
    /// The resolution the entities currently live in (Fatia 13): the
    /// production `applied_width_/applied_height_` pair. Starts at 0 so the
    /// first `apply_resolution_change` latches without scaling, and every
    /// later cascade divides by it (cumulative ratios).
    applied_w: i32,
    applied_h: i32,
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
    /// The session cheat flag (production `cheated_`): set by `apply_cheat`,
    /// read by the commit and debrief predicates. Production ALSO mirrors it
    /// into the score (`SetCheated`) -- both flags move together.
    cheated: bool,
    /// The one-shot commit guard (production `score_committed_`): the score
    /// commits at most once per session, whether the run ends by supernova,
    /// destruction or quit.
    score_committed: bool,
    /// Particle-details OSD countdown (production `details_osd_timer_`):
    /// `set_details_osd_timer` arms it, every motion half decrements it.
    details_osd_timer: i32,
    /// The largest window the session has seen (production
    /// `max_session_size_`, default {1280,720}); the Hall-of-Fame entry
    /// records the resolution the run actually played at. The resize cascade
    /// that updates it is Fatia 13.
    max_session_size: Coord,
    /// The pending-score ledger (production `ctx_.highscores`): the commit
    /// arms or cancels it, the shell's Update/consume cycle drains it.
    highscores: HighScores,
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
            applied_w: 0,
            applied_h: 0,
            rng: RandomStream::new(rebuild_seed),
            fanfare_durations,
            phase_frames: 0,
            phase_started: false,
            cheated: false,
            score_committed: false,
            details_osd_timer: 0,
            max_session_size: Coord::new(1280, 720),
            highscores: HighScores::new(),
        }
    }

    /// Runs exactly one frame of `CombatSession::TickFixedPhysics` (entity
    /// subset); the order below IS the golden contract. `dir` is the
    /// player's horizontal input (-1/0/+1) and `fire` mirrors the shell's
    /// `input_.Fire()` gate. Report-only composition: on a kamikaze-proximity
    /// or destruction frame this KEEPS TICKING (both halves run) and reports
    /// the flags -- the Fatia 5-11 corpora were recorded through exactly
    /// that behavior. The production-faithful early return lives in
    /// `tick_collisions`.
    pub fn tick(&mut self, dir: i32, fire: bool) -> TickReport {
        let c = self.collisions_core(false, false);
        let m = self.motion_core(dir, fire);
        TickReport {
            collisions: c.collisions,
            kamikaze_proximity: c.kamikaze_proximity,
            extra_shields: c.extra_shields,
            bomb_shield_loss: c.bomb_shield_loss,
            player_destroyed: c.player_destroyed,
            transition_started: m.transition_started,
            transition_frames: m.transition_frames,
            transition_remaining: m.transition_remaining,
            warp_triggered: m.warp_triggered,
            rebuilt: m.rebuilt,
            rebuild_level: m.rebuild_level,
            rebuild_seed: m.rebuild_seed,
        }
    }

    /// The collision half of one production frame, WITH the production early
    /// return: when the kamikaze probe fires, the frame ends right there --
    /// bonus collisions, the cheat block, the shield sync and every Move are
    /// skipped, exactly like TickFixedPhysics handing control to the
    /// supernova branch. `cheat_input` mirrors the shell's `input_.Cheat()`
    /// gate (the response runs at the production position: between the bonus
    /// collisions and the shield sync). Match-flow consumers pair this with
    /// `tick_motion` and the response methods; the ent.mf.nova/death corpora
    /// pin the split.
    pub fn tick_collisions(&mut self, cheat_input: bool) -> CollisionReport {
        self.collisions_core(true, cheat_input)
    }

    /// The motion half of one production frame: the phase-transition
    /// trigger, every manager advance, the rebuild-or-advance choice and the
    /// details-OSD decrement. Callers that got `kamikaze_proximity` or
    /// `player_destroyed` from `tick_collisions` must NOT call this for that
    /// frame -- production replaces the motion half with the cinematic
    /// response.
    pub fn tick_motion(&mut self, dir: i32, fire: bool) -> MotionReport {
        self.motion_core(dir, fire)
    }

    /// Shared collision-half implementation. `early_return_on_proximity`
    /// selects the contract: true = production TickFixedPhysics (the frame
    /// ends at the probe), false = the report-only composition `tick` was
    /// recorded with (the probe only sets the flag).
    fn collisions_core(&mut self, early_return_on_proximity: bool, cheat_input: bool) -> CollisionReport {
        // Every call touches disjoint members, so the borrow checker sees
        // the same sequence the C++ method runs on its member objects.
        // Fatia 11 wires the score economy into the kill chain: the smart
        // bonus roll reads the stage cap and the cycle mask, and a chosen
        // type is recorded before the pod drops -- the production `Score*`
        // member, threaded per call.
        let collisions = self.fleet.do_bullets_collisions(
            &mut self.bullets,
            &mut self.explosions,
            &mut self.bonuses,
            &mut self.sink,
            Some(&self.player),
            Some(&mut self.score),
        );
        self.kills += collisions.total_hits;
        // The production score-context block (TickFixedPhysics): credit the
        // frame's earnings, drain the extra-shield milestones and apply each
        // award to the player -- exactly between the two collision passes.
        let extra_shields = self.apply_score_context(collisions.score_earned);
        let old_shield = self.player.shield();
        self.player
            .do_bombs_collisions(&mut self.bombs, &mut self.explosions, &mut self.sink);
        let bomb_shield_loss = self.player.shield() < old_shield;
        // Production ends the frame here with the supernova cinematic; the
        // report-only composition carries the flag and continues.
        let kamikaze_proximity = self
            .fleet
            .check_kamikaze_proximity(self.player.position(), self.lethal_radius);
        if kamikaze_proximity && early_return_on_proximity {
            return CollisionReport {
                collisions,
                extra_shields,
                bomb_shield_loss,
                kamikaze_proximity,
                player_destroyed: false,
            };
        }
        self.player.do_bonus_collisions(
            &mut self.bonuses,
            &mut self.explosions,
            &mut self.sink,
            Some(&mut self.fleet),
            Some(&mut self.score),
        );
        // The production `input_.Cheat()` block sits between the bonus
        // collisions and the shield sync.
        if cheat_input {
            self.apply_cheat();
        }
        // Production syncs the score's shield mirror here (the bonus-mask
        // economy reads it) and ends the frame with the destruction cinematic
        // when the shield went negative; the kernel reports and continues.
        self.score.set_shield(self.player.shield());
        let player_destroyed = self.player.shield() < 0;
        CollisionReport {
            collisions,
            extra_shields,
            bomb_shield_loss,
            kamikaze_proximity,
            player_destroyed,
        }
    }

    /// Shared motion-half implementation (the TickFixedPhysics tail).
    fn motion_core(&mut self, dir: i32, fire: bool) -> MotionReport {
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
        // The details-OSD countdown ticks on every motion frame (production
        // decrements it on both TickFixedPhysics return paths).
        if self.details_osd_timer > 0 {
            self.details_osd_timer -= 1;
        }
        let transition_remaining = if held { self.phase_frames } else { 0 };
        MotionReport {
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

    // ---- match flow (Fatia 12): the branch bodies TickFixedPhysics runs
    // ---- when the collision half reports proximity or destruction, minus
    // ---- their Gfx/audio/input calls (shell territory).

    /// The cheat-input response (`input_.Cheat()` block): the session flag,
    /// the score disqualification mirror, the pending-ledger cancel and the
    /// three upgrades. Production has NO re-apply guard -- a second cheat
    /// stacks the upgrades up to the player caps (the ent.mf.cheat rows pin
    /// both passes). The flash and floating text are shell-side.
    /// Twin of CombatSession::ApplyResolutionChange (combat_session.cc),
    /// minus the shell tail (refresh-rate query, pacer reconfigure, pause
    /// flag -- device-dependent, out of kernel scope) and minus the starfield
    /// step: stars are a consumer-owned subsystem whose resize path is pinned
    /// separately (ent.stars rows, Fatia 9). The order IS the contract:
    /// guard chain, cumulative FLOAT ratios from the applied size, metrics
    /// distribution FIRST (the cascade's clamps must see the new viewport),
    /// then the per-entity cascade in canonical order, the lethal-proximity
    /// refresh and the per-axis max-session-size ratchet.
    pub fn apply_resolution_change(&mut self, nw: i32, nh: i32) -> ResizeOutcome {
        if nw <= 0 || nh <= 0 {
            return ResizeOutcome::Invalid;
        }
        if self.applied_w <= 0 || self.applied_h <= 0 {
            self.applied_w = nw;
            self.applied_h = nh;
            return ResizeOutcome::Latched;
        }
        if nw == self.applied_w && nh == self.applied_h {
            return ResizeOutcome::NoOp;
        }
        let prev = (self.applied_w, self.applied_h);
        let rx = nw as f32 / prev.0 as f32;
        let ry = nh as f32 / prev.1 as f32;
        let m = ViewportMetrics {
            win_w: nw,
            win_h: nh,
            scale: self.metrics.scale,
        };
        self.bullets.set_metrics(m);
        self.bombs.set_metrics(m);
        self.bonuses.set_metrics(m);
        self.explosions.set_metrics(m.scale);
        self.player.set_metrics(m);
        self.fleet.set_metrics(m);
        self.player.on_resize(rx, ry);
        self.fleet.on_resize(rx, ry);
        self.bullets.on_resize(rx, ry);
        self.bombs.on_resize(rx, ry);
        self.bonuses.on_resize(rx, ry);
        self.explosions.on_resize(rx, ry);
        // UpdateLethalProximity: the formation grid is NOT recomputed by a
        // resize (that belongs to the graphics-shell scale path), so the
        // refreshed radius reads the same static grid width at the unchanged
        // scale -- the ent.rz.resize rows pin that invariant.
        self.lethal_radius =
            formation::recompute(m.scale).width as f32 * KAMIKAZE_BLAST_RADIUS_MULTIPLIER;
        self.max_session_size.x = self.max_session_size.x.max(nw);
        self.max_session_size.y = self.max_session_size.y.max(nh);
        self.metrics = m;
        self.applied_w = nw;
        self.applied_h = nh;
        ResizeOutcome::Applied {
            rx,
            ry,
            prev_applied: prev,
        }
    }

    pub fn apply_cheat(&mut self) {
        self.cheated = true;
        self.score.set_cheated(true);
        self.highscores.cancel_pending();
        self.player.extra_fire();
        self.player.extra_shield();
        self.player.extra_multi();
    }

    /// `CommitFinalScore` twin, minus the ledger I/O: the guard is
    /// unconditional (production sets `score_committed_` BEFORE the
    /// predicate), an honest positive score arms the pending ledger -- stage
    /// = cycle, wave = level, window = max session size, exactly the run-end
    /// snapshot -- and anything else cancels it. Draining the ledger into a
    /// `HighScoreTable` is the caller's step (`HighScores::consume_pending`,
    /// the production `Update()` I/O half). Returns whether THIS call
    /// committed a score.
    pub fn commit_final_score(&mut self, refresh_rate: i32) -> bool {
        if self.score_committed {
            return false;
        }
        self.score_committed = true;
        if !self.cheated && self.score.value() > 0 {
            self.highscores.add(
                self.score.value(),
                self.max_session_size,
                refresh_rate,
                self.score.cycle(),
                self.score.level(),
            );
            true
        } else {
            self.highscores.cancel_pending();
            false
        }
    }

    /// The TickFixedPhysics kamikaze branch, kernel subset: the score's
    /// shield mirror drops to -1 (the HUD reads death before the player
    /// entity does), the guarded commit runs, and the screen-wide nova
    /// bursts. Returns `committed`. Flash/trauma/SFX/rumble are shell calls;
    /// the 3 s cinematic loop that follows is driven frame by frame with
    /// `supernova_cinematic_tick`.
    pub fn kamikaze_supernova_response(&mut self, refresh_rate: i32) -> bool {
        self.score.set_shield(-1);
        let committed = self.commit_final_score(refresh_rate);
        let pos = self.player.position();
        let (w, h) = (self.metrics.win_w, self.metrics.win_h);
        self.explosions.trigger_screen_wide_nova(pos, w, h);
        committed
    }

    /// The shield-below-zero branch, kernel subset: the guarded commit, then
    /// the two canonical hull bursts (`ExecuteGrandPlayerDestruction`'s
    /// simulation half). Returns `committed`. The destruction SFX, flash and
    /// rumble are shell calls; the 1.35 s loop is driven frame by frame with
    /// `destruction_cinematic_tick`.
    pub fn grand_destruction_response(&mut self, refresh_rate: i32) -> bool {
        let committed = self.commit_final_score(refresh_rate);
        let pos = self.player.position();
        self.explosions
            .add(pos, Coord::new(0, -1), 255, 255, 230, 70);
        self.explosions
            .add(pos, Coord::new(0, 1), 255, 210, 50, 65);
        committed
    }

    /// One supernova-cinematic frame (`ExecuteKamikazeSupernova` body):
    /// explosions only. The overlay text and score drawing are shell-side.
    pub fn supernova_cinematic_tick(&mut self) {
        self.explosions.advance();
    }

    /// One destruction-cinematic frame (`ExecuteGrandPlayerDestruction`
    /// body): bonuses, bullets, bombs (default Coord(-1,-1) turn target --
    /// production calls `bombs_.Move()` with no argument) and explosions
    /// advance; the armada is Draw-only in production, so it does NOT
    /// advance -- the frozen fleet is part of the pinned contract.
    pub fn destruction_cinematic_tick(&mut self) {
        self.bonuses.advance();
        self.bullets.advance(Coord::new(-1, -1));
        self.bombs.advance(Coord::new(-1, -1));
        self.explosions.advance();
    }

    /// The debriefing new-record predicate (`EnterDebriefing`): an honest
    /// run with a positive score.
    pub fn debrief_new_record(&self) -> bool {
        !self.cheated && self.score.value() > 0
    }

    /// The debriefing status tri-state: 2 = cheated (disqualified), 1 = new
    /// record, 0 = run ended without a record. Colors and banner strings are
    /// shell-side.
    pub fn debrief_status_kind(&self) -> i32 {
        if self.cheated {
            2
        } else if self.debrief_new_record() {
            1
        } else {
            0
        }
    }

    /// The `input_.Details()` response: arm the OSD countdown for 1.5 s of
    /// refresh-rate frames. `tick_motion`/`tick` decrement it per frame.
    pub fn set_details_osd_timer(&mut self, refresh_rate: i32) {
        self.details_osd_timer = details_osd_frames(refresh_rate);
    }

    pub fn details_osd_timer(&self) -> i32 {
        self.details_osd_timer
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

    /// Mutable access to the economy: the save/continue wiring point (a
    /// resumed session restores level, mask and ledger) and the golden
    /// harness's prelude entry (`ent.fb.prelude` rows drive it).
    pub fn score_mut(&mut self) -> &mut Score {
        &mut self.score
    }

    /// Lifetime alien kills (regular + kamikaze).
    pub fn kills(&self) -> i32 {
        self.kills
    }

    /// The pending-score ledger (production `ctx_.highscores`).
    pub fn highscores(&self) -> &HighScores {
        &self.highscores
    }

    /// Mutable ledger access: the shell's Update/consume cycle (production
    /// `HighScores::Update` is the I/O half `consume_pending` mirrors) and
    /// the golden harness's script entry (`hs.*` rows drive it).
    pub fn highscores_mut(&mut self) -> &mut HighScores {
        &mut self.highscores
    }

    /// The resolution the entities currently live in (Fatia 13 observability).
    pub fn applied_size(&self) -> (i32, i32) {
        (self.applied_w, self.applied_h)
    }

    /// Kamikaze lethal proximity radius in design pixels (Fatia 13: the
    /// resize cascade refreshes it; the ent.rz.resize rows pin the value).
    pub fn lethal_radius(&self) -> f32 {
        self.lethal_radius
    }

    // Scenario injectors (Fatia 13): the resize corpus scripts turning bombs,
    // a relativistic oscillator, a supply pod and debris bursts at fixed
    // frames -- the same role the dumper's direct manager calls play on the
    // C++ side. Mutable access limited to the entity managers, mirroring the
    // score_mut/highscores_mut harness precedent.
    pub fn bullets_mut(&mut self) -> &mut BulletsManager {
        &mut self.bullets
    }

    pub fn bombs_mut(&mut self) -> &mut BulletsManager {
        &mut self.bombs
    }

    pub fn bonuses_mut(&mut self) -> &mut BonusManager {
        &mut self.bonuses
    }

    pub fn explosions_mut(&mut self) -> &mut ExplosionsManager {
        &mut self.explosions
    }

    pub fn max_session_size(&self) -> Coord {
        self.max_session_size
    }

    pub fn cheated(&self) -> bool {
        self.cheated
    }

    pub fn score_committed(&self) -> bool {
        self.score_committed
    }

    /// The sink records by design: the consumer drains it every frame (the
    /// replay harness drains into corpus rows; the Phase-4 shell will drain
    /// into presentation).
    pub fn sink_mut(&mut self) -> &mut EventSink {
        &mut self.sink
    }
}
