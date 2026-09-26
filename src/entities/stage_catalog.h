#ifndef STAGE_CATALOG_H
#define STAGE_CATALOG_H

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "constants.h"

namespace GameRules {

/**
 * @struct StageProfile
 * @brief Immutable compile-time descriptor of wave kinematics and combat economy.
 * Pre-calculated at compile-time to eliminate repeated divisions, branches, and modulos in the 60 Hz loop.
 */
struct StageProfile {
  int wave{1};
  int stage_cycle{1};
  int convoy_count{5};
  int max_bombs{3};
  float cycle_bomb_mult{1.0f};
  float fleet_speed{6.0f};
  int max_attack_wait_frames{300};
  int kamikaze_quota{1};
  bool is_radioactive_wave{false};
  bool is_relativistic_wave{false};
  bool wanderers_allowed{false};
};

/**
 * @class StageCatalog
 * @brief Compile-time evaluation and O(1) instant lookup of stage parameters for all 15 stages.
 */
class StageCatalog {
 public:
  StageCatalog() = delete;

  [[nodiscard]] static constexpr StageProfile GetProfile(int wave, int max_levels = 15) noexcept {
    const int clamped_wave = (wave < 1) ? 1 : wave;
    const int cycle = Progression::WaveToStage(clamped_wave);

    StageProfile p{};
    p.wave = clamped_wave;
    p.stage_cycle = cycle;

    // 1. Convoy counts for the 15 stages (cycle 1 vs cycle 2+)
    constexpr int kCountsS1[15] = {5, 6, 4, 6, 5, 6, 5, 5, 5, 6, 4, 5, 10, 5, 5};
    constexpr int kCountsS2[15] = {4, 4, 3, 4, 4, 4, 3, 3, 3, 4, 3, 3, 3, 5, 4};
    const std::size_t lvl_idx = static_cast<std::size_t>((clamped_wave - 1) % Progression::kWavesPerStage);
    p.convoy_count = (cycle >= 2) ? kCountsS2[lvl_idx] : kCountsS1[lvl_idx];

    // 2. Compile-time bomb ceiling limit: min(8, 3 + wave / 3)
    p.max_bombs = std::min(Combat::kMaxBombsPerStage,
                           Combat::kBaseBombsPerStage + clamped_wave / Combat::kBombsPerStageLevelDivisor);

    // 3. Stage cycle projectile speed multiplier
    if (cycle == 1) {
      p.cycle_bomb_mult = 1.0f;
    } else if (cycle == 2) {
      p.cycle_bomb_mult = 1.25f;
    } else {
      p.cycle_bomb_mult = std::min(Combat::kCycleBombMultMax,
                                   Combat::kCycleBombMultBase + static_cast<float>(cycle - 3) * Combat::kCycleBombMultIncrement);
    }

    // 4. Fleet trajectory velocity
    p.fleet_speed = Fleet::ComputeSpeed(clamped_wave, max_levels);

    // 5. Attack dive wait ceiling in frames
    p.max_attack_wait_frames = Fleet::GetMaxAttackWaitFrames(clamped_wave);

    // 6. Active Kamikaze quota
    p.kamikaze_quota = Fleet::GetKamikazeQuota(clamped_wave);

    // 7. Special entity wave flags (Stage 4 = Curie, Stage 14 = Einstein)
    const int stage_offset = clamped_wave % Progression::kWavesPerStage;
    p.is_radioactive_wave = (stage_offset == 4);
    p.is_relativistic_wave = (stage_offset == 14);

    // 8. Wanderers unlocked in Stage 2+
    p.wanderers_allowed = (cycle >= 2);

    return p;
  }
};

}  // namespace GameRules

#endif  // STAGE_CATALOG_H
