#ifndef SCORE_H
#define SCORE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "constants.h"
#include "gfxinterface.h"

class Score {
  uint64_t score_{0};
  double shield_progress_{0.0};
  int shield_{GameRules::Player::kInitialShields};
  int level_{1};
  int stage_bonus_cycle_{0};
  uint32_t stage_spawned_bonuses_mask_{0};
  bool cheated_{false};

  mutable char cached_score_str_[160]{};
  mutable char cached_shields_str_[32]{};
  mutable float cached_shields_w_{0.0f};
  mutable uint64_t last_drawn_score_{UINT64_MAX};
  mutable int last_drawn_level_{-1};
  mutable int last_drawn_shield_{-999};
  mutable int last_drawn_cycle_{-1};
  mutable float last_drawn_scale_{-1.0f};
  mutable bool last_drawn_cheated_{false};

  // Step 18: DrawLevel() banner text only changes when the wave/stage
  // advances, not every frame of the multi-second transition animation.
  mutable int         cached_level_strings_for_{-1};
  mutable std::string cached_wave_title_;
  mutable std::string cached_stage_sub_;
  mutable int         cached_fanfare_for_{-1};
  mutable std::string cached_track_line_;
  mutable std::string cached_curiosity_line_;

 public:
  Score();
  ~Score() = default;

  [[nodiscard]] uint64_t Value() const noexcept { return score_; }
  [[nodiscard]] int Level() const noexcept { return level_; }
  [[nodiscard]] int Cycle() const noexcept { return GameRules::Progression::WaveToStage(level_); }
  [[nodiscard]] int Height() const noexcept { return 36; }
  [[nodiscard]] double ShieldProgress() const noexcept { return shield_progress_; }
  void SetCheated(bool c) noexcept { cheated_ = c; }
  [[nodiscard]] bool IsCheated() const noexcept { return cheated_; }

  void IncLevel() noexcept { ++level_; }
  void SetShield(int i) noexcept { shield_ = std::min(GameRules::Player::kPlayerMaxShield, i); }
  [[nodiscard]] int Shield() const noexcept { return shield_; }

  /// Checks whether a specific bonus type (1..5) is still eligible to spawn in the current stage cycle.
  /// Enforces a hard maximum of 5 bonuses per stage and zero duplicate bonus types within the same stage.
  [[nodiscard]] bool CanSpawnBonusType(int bonus_type) const noexcept {
    if (bonus_type <= 0 || bonus_type > GameRules::Combat::kMaxBonusesPerStage) return false;
    if (stage_bonus_cycle_ != Cycle()) return true;
    return (stage_spawned_bonuses_mask_ & (1u << static_cast<uint32_t>(bonus_type))) == 0;
  }

  /// Records that a bonus type has spawned in the current stage cycle, locking it out from subsequent waves.
  void RecordBonusSpawned(int bonus_type) noexcept {
    if (bonus_type <= 0 || bonus_type > GameRules::Combat::kMaxBonusesPerStage) return;
    if (stage_bonus_cycle_ != Cycle()) {
      stage_bonus_cycle_ = Cycle();
      stage_spawned_bonuses_mask_ = 0;
    }
    stage_spawned_bonuses_mask_ |= (1u << static_cast<uint32_t>(bonus_type));
  }

  /// Returns total number of distinct bonuses spawned so far in the current 15-wave stage cycle.
  [[nodiscard]] int StageBonusesSpawnedCount() const noexcept {
    if (stage_bonus_cycle_ != Cycle()) return 0;
    int count = 0;
    for (int b = 1; b <= GameRules::Combat::kMaxBonusesPerStage; ++b) {
      if ((stage_spawned_bonuses_mask_ & (1u << static_cast<uint32_t>(b))) != 0) {
        ++count;
      }
    }
    return count;
  }


  [[nodiscard]] uint64_t ExtraShieldStep() const noexcept {
    // Pure arcade fairness: exactly 1000 points per shield milestone across all resolutions.
    return GameRules::Combat::kExtraShieldScoreStep;
  }

  void Add(uint64_t pts) noexcept {
    if (pts > 0) {
      if (score_ > UINT64_MAX - pts) {
        score_ = UINT64_MAX;
      } else {
        score_ += pts;
      }
      const uint64_t step = ExtraShieldStep();
      if (step > 0) {
        shield_progress_ += static_cast<double>(pts) / static_cast<double>(step);
      }
    }
  }

  [[nodiscard]] uint64_t CheckExtraShieldMilestones() noexcept {
    uint64_t count = 0;
    while (shield_progress_ >= 1.0) {
      ++count;
      shield_progress_ -= 1.0;
    }
    return count;
  }

  [[nodiscard]] uint64_t NextExtraShieldScore() const noexcept {
    const uint64_t step = ExtraShieldStep();
    const double remaining = std::max(0.0, 1.0 - shield_progress_);
    const uint64_t needed = static_cast<uint64_t>(
        FastRound(remaining * static_cast<double>(step)));
    if (score_ > UINT64_MAX - needed) {
      return UINT64_MAX;
    }
    return score_ + needed;
  }

  void ReInit() noexcept {
    score_ = 0;
    level_ = 1;
    shield_ = GameRules::Player::kInitialShields;
    shield_progress_ = 0.0;
    stage_bonus_cycle_ = 0;
    stage_spawned_bonuses_mask_ = 0;
    cheated_ = false;
    last_drawn_score_ = UINT64_MAX;
    cached_fanfare_for_ = -1;
  }

  void Draw();
  void DrawLevel(float alpha = 1.0f) const;
};

#endif  // SCORE_H
