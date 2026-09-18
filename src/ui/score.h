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
  double life_progress_{0.0};
  int shield_{GameRules::Player::kInitialShieldLives};
  int level_{1};
  int nuke_spawned_cycle_{0};
  bool cheated_{false};

  mutable char cached_score_str_[160]{};
  mutable char cached_lives_str_[32]{};
  mutable float cached_lives_w_{0.0f};
  mutable uint64_t last_drawn_score_{UINT64_MAX};
  mutable int last_drawn_level_{-1};
  mutable int last_drawn_shield_{-999};
  mutable uint64_t last_drawn_1up_{UINT64_MAX};
  mutable float last_drawn_scale_{-1.0f};
  mutable bool last_drawn_cheated_{false};

  // Step 18: DrawLevel() banner text only changes when the wave/stage
  // advances, not every frame of the multi-second transition animation.
  mutable int         cached_level_strings_for_{-1};
  mutable std::string cached_wave_title_;
  mutable std::string cached_stage_sub_;

 public:
  Score();
  ~Score() = default;

  [[nodiscard]] uint64_t Value() const noexcept { return score_; }
  [[nodiscard]] int Level() const noexcept { return level_; }
  [[nodiscard]] int Cycle() const noexcept { return GameRules::Progression::WaveToStage(level_); }
  [[nodiscard]] int Height() const noexcept { return 36; }
  [[nodiscard]] double LifeProgress() const noexcept { return life_progress_; }
  void SetCheated(bool c) noexcept { cheated_ = c; }
  [[nodiscard]] bool IsCheated() const noexcept { return cheated_; }

  void IncLevel() noexcept { ++level_; }
  void SetShield(int i) noexcept { shield_ = std::min(GameRules::Player::kPlayerMaxShield, i); }
  [[nodiscard]] int Shield() const noexcept { return shield_; }

  [[nodiscard]] bool CanSpawnNukeInCurrentCycle() const noexcept {
    return nuke_spawned_cycle_ != Cycle();
  }
  void RecordNukeSpawnedInCurrentCycle() noexcept {
    nuke_spawned_cycle_ = Cycle();
  }

  [[nodiscard]] uint64_t ExtraLifeStep() const noexcept {
    const int w = Gfx::Inst().WindowWidth();
    const uint64_t w_val = static_cast<uint64_t>(std::max(1280, w));
    const uint64_t step = (GameRules::Combat::kExtraLifeScoreStep * w_val) / static_cast<uint64_t>(1280);
    return ((step + static_cast<uint64_t>(25)) / static_cast<uint64_t>(50)) * static_cast<uint64_t>(50);
  }

  void Add(uint64_t pts) noexcept {
    if (pts > 0) {
      if (score_ > UINT64_MAX - pts) {
        score_ = UINT64_MAX;
      } else {
        score_ += pts;
      }
      const uint64_t step = ExtraLifeStep();
      if (step > 0) {
        life_progress_ += static_cast<double>(pts) / static_cast<double>(step);
      }
    }
  }

  [[nodiscard]] uint64_t CheckExtraLifeMilestones() noexcept {
    uint64_t count = 0;
    while (life_progress_ >= 1.0) {
      ++count;
      life_progress_ -= 1.0;
    }
    return count;
  }

  [[nodiscard]] uint64_t NextExtraLifeScore() const noexcept {
    const uint64_t step = ExtraLifeStep();
    const double remaining = std::max(0.0, 1.0 - life_progress_);
    const uint64_t needed = static_cast<uint64_t>(
        std::round(remaining * static_cast<double>(step)));
    if (score_ > UINT64_MAX - needed) {
      return UINT64_MAX;
    }
    return score_ + needed;
  }

  void ReInit() noexcept {
    score_ = 0;
    level_ = 1;
    shield_ = GameRules::Player::kInitialShieldLives;
    life_progress_ = 0.0;
    nuke_spawned_cycle_ = 0;
    cheated_ = false;
    last_drawn_score_ = UINT64_MAX;
  }

  void Draw();
  void DrawLevel(float alpha = 1.0f) const;
};

#endif  // SCORE_H
