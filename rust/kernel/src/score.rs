//! Score economy kernel (Fatia 7): the in-game Score context -- saturating
//! point accumulation, the fractional extra-shield milestone ledger, the
//! shield display value, wave-to-stage progression and the per-stage bonus
//! economy mask.
//!
//! Twin of the pure-logic surface of the C++ `src/ui/score.h`. The draw state
//! (HUD plate caches, debounce clocks, cached banner strings) stays in the
//! shell: this twin models only the columns pinned by the `score.*` golden
//! section. C++ `ReInit` also resets a couple of draw caches; those fields do
//! not exist here, so `reinit` covers exactly the pure subset.
//!
//! The bonus-economy methods are the gate `AliensManager::RollSmartBonusType`
//! consults through its `Score*` (the corpus fleet scenario wires that pointer
//! null; a score-wired fleet replay consumes these methods directly).

use crate::stage_catalog::wave_to_stage;

/// `GameRules::Player::kInitialShields` -- pinned by the `score.consts` row.
pub const INITIAL_SHIELDS: i32 = 3;
/// `GameRules::Player::kPlayerMaxShield` -- pinned by the `score.consts` row.
pub const MAX_SHIELD: i32 = 8;
/// `GameRules::Combat::kExtraShieldScoreStep` -- pinned by the `score.consts` row.
pub const EXTRA_SHIELD_STEP: u64 = 1000;
/// `GameRules::Combat::kMaxBonusesPerStage` -- pinned by the `score.consts` row.
pub const MAX_BONUSES_PER_STAGE: i32 = 5;

/// The pure Score context: points, milestone ledger, shield, wave level and
/// the bonus-economy mask of the current stage cycle.
#[derive(Debug)]
pub struct Score {
    score: u64,
    shield_progress: f64,
    shield: i32,
    level: i32,
    stage_bonus_cycle: i32,
    stage_spawned_bonuses_mask: u32,
    cheated: bool,
}

impl Default for Score {
    fn default() -> Self {
        // The C++ constructor is `Score() { ReInit(); }` -- identical fields.
        Self {
            score: 0,
            shield_progress: 0.0,
            shield: INITIAL_SHIELDS,
            level: 1,
            stage_bonus_cycle: 0,
            stage_spawned_bonuses_mask: 0,
            cheated: false,
        }
    }
}

impl Score {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    #[must_use]
    pub fn value(&self) -> u64 {
        self.score
    }
    #[must_use]
    pub fn level(&self) -> i32 {
        self.level
    }
    /// `Progression::WaveToStage(level)` -- reuses the catalog twin (SSOT).
    #[must_use]
    pub fn cycle(&self) -> i32 {
        wave_to_stage(self.level)
    }
    pub fn set_cheated(&mut self, cheated: bool) {
        self.cheated = cheated;
    }
    #[must_use]
    pub fn cheated(&self) -> bool {
        self.cheated
    }
    pub fn inc_level(&mut self) {
        self.level += 1;
    }
    /// Upper clamp only: the negative destruction sentinel must survive
    /// (kamikaze teardown sets shield below zero on purpose).
    pub fn set_shield(&mut self, shield: i32) {
        self.shield = MAX_SHIELD.min(shield);
    }
    #[must_use]
    pub fn shield(&self) -> i32 {
        self.shield
    }

    /// Bonus types are 1..=5 (`Bonus::extra_*`); a stale cycle tag means the
    /// mask belongs to an earlier stage, so every type is eligible again.
    #[must_use]
    pub fn can_spawn_bonus_type(&self, bonus_type: i32) -> bool {
        if bonus_type <= 0 || bonus_type > MAX_BONUSES_PER_STAGE {
            return false;
        }
        if self.stage_bonus_cycle != self.cycle() {
            return true;
        }
        (self.stage_spawned_bonuses_mask & (1u32 << bonus_type as u32)) == 0
    }

    pub fn record_bonus_spawned(&mut self, bonus_type: i32) {
        if bonus_type <= 0 || bonus_type > MAX_BONUSES_PER_STAGE {
            return;
        }
        if self.stage_bonus_cycle != self.cycle() {
            self.stage_bonus_cycle = self.cycle();
            self.stage_spawned_bonuses_mask = 0;
        }
        self.stage_spawned_bonuses_mask |= 1u32 << bonus_type as u32;
    }

    #[must_use]
    pub fn stage_bonuses_spawned_count(&self) -> i32 {
        if self.stage_bonus_cycle != self.cycle() {
            return 0;
        }
        let mut count = 0;
        for bit in 1..=MAX_BONUSES_PER_STAGE {
            if (self.stage_spawned_bonuses_mask & (1u32 << bit as u32)) != 0 {
                count += 1;
            }
        }
        count
    }

    #[must_use]
    pub fn extra_shield_step() -> u64 {
        EXTRA_SHIELD_STEP
    }

    pub fn add(&mut self, pts: u64) {
        if pts > 0 {
            // Saturating accumulate; the C++ guard is `score_ > UINT64_MAX - pts`.
            if self.score > u64::MAX - pts {
                self.score = u64::MAX;
            } else {
                self.score += pts;
            }
            // Same IEEE division as the C++ `double(pts) / double(step)`.
            self.shield_progress += pts as f64 / EXTRA_SHIELD_STEP as f64;
        }
    }

    /// Subtractive drain (not a division): matches the C++ loop op for op, so
    /// accumulated floating-point residue resolves identically on both sides.
    /// O(count) by design -- production magnitudes are tiny; the corpus only
    /// pins (never drains) the residue of the saturating Add.
    pub fn check_extra_shield_milestones(&mut self) -> u64 {
        let mut count = 0u64;
        while self.shield_progress >= 1.0 {
            count += 1;
            self.shield_progress -= 1.0;
        }
        count
    }

    pub fn reinit(&mut self) {
        self.score = 0;
        self.level = 1;
        self.shield = INITIAL_SHIELDS;
        self.shield_progress = 0.0;
        self.stage_bonus_cycle = 0;
        self.stage_spawned_bonuses_mask = 0;
        self.cheated = false;
    }

    // Golden-vector observation accessors (the score.* state columns).
    #[must_use]
    pub fn shield_progress(&self) -> f64 {
        self.shield_progress
    }
    #[must_use]
    pub fn stage_bonus_mask(&self) -> u32 {
        self.stage_spawned_bonuses_mask
    }
    #[must_use]
    pub fn stage_bonus_cycle(&self) -> i32 {
        self.stage_bonus_cycle
    }
}
