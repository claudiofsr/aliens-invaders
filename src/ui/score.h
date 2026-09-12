#ifndef SCORE_H
#define SCORE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "gfxinterface.h"

/**
 * @class Score
 * @brief Thread-safe telemetry for scoring, extra-life progression, and cycle nuke gating.
 */
class Score {
  static Score* singleton_;
  uint64_t score_{0};
  double life_progress_{0.0};
  int shield_{3};
  int level_{1};
  int nuke_spawned_cycle_{0};
  bool cheated_{false};

  Score();
  ~Score() = default;

 public:
  static Score& Instance();
  static void DestroyInstance();

  [[nodiscard]] uint64_t Value() const noexcept { return score_; }
  [[nodiscard]] int Level() const noexcept { return level_; }
  [[nodiscard]] int Cycle() const noexcept { return ((level_ - 1) / 15) + 1; }
  [[nodiscard]] int Height() const noexcept { return 36; }
  [[nodiscard]] double LifeProgress() const noexcept { return life_progress_; }
  void SetCheated(bool c) noexcept { cheated_ = c; }
  [[nodiscard]] bool IsCheated() const noexcept { return cheated_; }

  void IncLevel() noexcept { ++level_; }
  void SetShield(int i) noexcept { shield_ = std::min(8, i); }
  [[nodiscard]] int Shield() const noexcept { return shield_; }

  [[nodiscard]] bool CanSpawnNukeInCurrentCycle() const noexcept {
    return nuke_spawned_cycle_ != Cycle();
  }
  void RecordNukeSpawnedInCurrentCycle() noexcept {
    nuke_spawned_cycle_ = Cycle();
  }

  [[nodiscard]] uint64_t ExtraLifeStep() const noexcept {
    const int w = Gfx::Inst().WindowWidth();
    const uint64_t step =
        (1000ULL * static_cast<uint64_t>(std::max(1280, w))) / 1280ULL;
    return ((step + 25ULL) / 50ULL) * 50ULL;
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
    shield_ = 3;
    life_progress_ = 0.0;
    nuke_spawned_cycle_ = 0;
    cheated_ = false;
  }

  void Draw();
  void DrawLevel() const;
};

#endif  // SCORE_H
