#ifndef GAME_RULES_H
#define GAME_RULES_H

#include <algorithm>
#include <cmath>
#include <cstdint>

/**
 * @class GameRules
 * @brief Pure functional, deterministic, side-effect-free rules engine for Aliens Invaders.
 * Centralizes gameplay kinetics, loot probabilities, cycle progression, and anti-scissor bounds.
 */
class GameRules {
 public:
  // Disallow instantiation (Pure static domain engine)
  GameRules() = delete;

  // ============================================================================
  // Earth Vessel Kinetic Parameters
  // ============================================================================
  static constexpr float kPlayerBaseSpeed = 3.5f;        // Reduced by 30% (was 5.0f)
  static constexpr float kPlayerBaseBulletSpeed = -8.0f; // Reduced by 50% (was -16.0f)

  // 50% reduction in initial shots per second (Interval doubled from 20.0f -> 40.0f frames)
  // At 60 FPS: 60 / 40 = 1.5 shots per second
  static constexpr float kPlayerBaseFireInterval = 40.0f;

  static constexpr int kPlayerMaxSpeedLevel = 2;         // 120% max (Level 0=100%, 1=110%, 2=120%)
  static constexpr int kPlayerMaxFireLevel = 2;          // 120% max (Level 0=100%, 1=110%, 2=120%)
  static constexpr int kPlayerMaxMultiShots = 3;         // 3 simultaneous shots max
  static constexpr int kPlayerMaxShield = 8;             // 8 lives max
  static constexpr int kPlayerShieldGateThreshold = 4;   // Shield bonus drops ONLY if lives <= 4

  [[nodiscard]] static constexpr float ComputePlayerSpeed(int speed_level) noexcept {
    const int clamped = std::clamp(speed_level, 0, kPlayerMaxSpeedLevel);
    return kPlayerBaseSpeed * (1.0f + 0.10f * static_cast<float>(clamped));
  }

  [[nodiscard]] static constexpr int ComputeFireInterval(int fire_level) noexcept {
    const int clamped = std::clamp(fire_level, 0, kPlayerMaxFireLevel);
    const float rate_mult = 1.0f + 0.10f * static_cast<float>(clamped);
    return std::max(8, static_cast<int>(std::round(kPlayerBaseFireInterval / rate_mult)));
  }

  // ============================================================================
  // Alien Fleet Kinetics & Difficulty Scaling
  // ============================================================================
  [[nodiscard]] static constexpr float ComputeAlienSpeed(int level, int max_levels) noexcept {
    const int cycle = (level - 1) / std::max(1, max_levels) + 1;
    const int stage_in_cycle = (level - 1) % std::max(1, max_levels);
    const float intra = static_cast<float>(stage_in_cycle) / static_cast<float>(max_levels);

    if (cycle == 1) {
      return 4.8f + intra * 1.8f; // Cycle 1: Medium (4.8 to 6.6 px/f)
    } else if (cycle == 2) {
      return 7.0f + intra * 2.2f; // Cycle 2: Hard (7.0 to 9.2 px/f)
    } else {
      const float cycle_add = std::min(2.0f, static_cast<float>(cycle - 3) * 0.6f);
      return std::min(12.5f, 9.5f + intra * 2.5f + cycle_add); // Cycle 3+: Super Hard (9.5 to 12.5 px/f)
    }
  }

  // Parametric Attack Delay per Cycle (0 to 5s, 0 to 4s, 0 to 3s, 0 to 2s)
  [[nodiscard]] static constexpr int GetMaxAttackWaitFrames(int level) noexcept {
    const int cycle = (level - 1) / 15 + 1;
    if (cycle == 1) return 5 * 60; // 300 frames (5.0s)
    if (cycle == 2) return 4 * 60; // 240 frames (4.0s)
    if (cycle == 3) return 3 * 60; // 180 frames (3.0s)
    return 2 * 60;                 // 120 frames (2.0s) for Cycle 4+
  }

  // ============================================================================
  // Ballistics & Vector Missiles
  // ============================================================================
  static constexpr int kMinVectorMissiles = 2;
  static constexpr int kMaxVectorMissiles = 10;
  static constexpr float kMaxDeflectionAngleDeg = 15.0f;
  static constexpr int kSafeCorridorBasePx = 118; // Safe evasion width
};

#endif  // GAME_RULES_H
