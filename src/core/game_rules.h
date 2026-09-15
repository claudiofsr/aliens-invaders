#ifndef GAME_RULES_H
#define GAME_RULES_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace GameRules {

// ==============================================================================
// 1. Simulation Clock & Pacing (Deterministic 60 Hz Physics)
// ==============================================================================
namespace Simulation {
  static constexpr float kSimulationFrequencyHz = 60.0f;
  static constexpr double kFixedStepDurationSec = 1.0 / static_cast<double>(kSimulationFrequencyHz);
  static constexpr uint64_t kFixedStepNanoseconds = 16'666'667ULL;
  static constexpr double kMaxFrameDeltaLimitSec = 0.250; // 250 milliseconds (0.25 s)
  static constexpr uint64_t kMaxFrameDeltaNanoseconds = 250'000'000ULL;
  static constexpr uint64_t kMaxAccumulatorNanoseconds = kFixedStepNanoseconds * 2ULL; // Max 33.3ms backlog

  // Aliases
  static constexpr float kSimulationHz = kSimulationFrequencyHz;
  static constexpr uint64_t kFixedStepNs = kFixedStepNanoseconds;
  static constexpr uint64_t kMaxFrameDeltaNs = kMaxFrameDeltaNanoseconds;
  static constexpr uint64_t kMaxAccumulatorNs = kMaxAccumulatorNanoseconds;
}

// ==============================================================================
// 2. Stage Progression & Hyperspace Warp (15 Waves per Stage Cycle)
// ==============================================================================
namespace Progression {
  static constexpr int kWavesPerStage = 15;
  static constexpr float kStageFanfareDurationSec = 5.0f;       // Stage fanfare length (seconds)
  static constexpr float kPhaseTransitionMinDurationSec = 5.0f; // Armadas transition pause (seconds)
  static constexpr float kHyperspaceWarpDurationSec = 6.0f;     // Warp star streaming (seconds)
  static constexpr float kHyperspaceWarpSpeedMultiplier = 9.0f; // Stars speed multiplier in warp

  [[nodiscard]] inline constexpr int WaveToStage(int wave) noexcept {
    return ((wave - 1) / kWavesPerStage) + 1;
  }
  [[nodiscard]] inline constexpr int WaveInStage(int wave) noexcept {
    return (wave - 1) % kWavesPerStage;
  }

  // Aliases
  static constexpr float kFanfareSec = kStageFanfareDurationSec;
  static constexpr float kTransitionMinSec = kPhaseTransitionMinDurationSec;
  static constexpr float kStageFanfareDurationSeconds = kStageFanfareDurationSec;
  static constexpr float kPhaseTransitionMinSeconds = kPhaseTransitionMinDurationSec;
  static constexpr float kWarpDurationSeconds = kHyperspaceWarpDurationSec;
  static constexpr float kWarpSpeedFactor = kHyperspaceWarpSpeedMultiplier;
  static constexpr float kWarpSpeedMult = kHyperspaceWarpSpeedMultiplier;
}

// ==============================================================================
// 3. Player Vessel Kinetics & Upgrades
// ==============================================================================
namespace Player {
  static constexpr float kBaseSpeed = 3.5f;
  static constexpr float kBaseBulletSpeed = -8.0f;
  static constexpr float kBaseFireIntervalFrames = 40.0f;

  static constexpr int kInitialShieldLives = 3;
  static constexpr int kMaxShieldLives = 8;
  static constexpr int kShieldGateThreshold = 4;

  static constexpr int kMaxSpeedLevel = 2;
  static constexpr int kMaxFireLevel = 2;
  static constexpr int kMaxMultiShots = 3;

  static constexpr int kDamageImmunityFrames = 42;
  static constexpr float kSpeedBoostPercent = 0.10f;    // +10% speed per upgrade
  static constexpr float kFireRateBoostPercent = 0.10f; // +10% fire rate per upgrade
  static constexpr float kCollisionHitboxScale = 0.65f; // Hitbox reduction factor (65%)

  [[nodiscard]] inline constexpr float ComputeSpeed(int speed_level) noexcept {
    const int clamped = std::clamp(speed_level, 0, kMaxSpeedLevel);
    return kBaseSpeed * (1.0f + kSpeedBoostPercent * static_cast<float>(clamped));
  }

  [[nodiscard]] inline constexpr int ComputeFireInterval(int fire_level) noexcept {
    const int clamped = std::clamp(fire_level, 0, kMaxFireLevel);
    const float rate_mult = 1.0f + kFireRateBoostPercent * static_cast<float>(clamped);
    return std::max(8, static_cast<int>(std::round(kBaseFireIntervalFrames / rate_mult)));
  }

  // Aliases
  static constexpr float kBaseFireInterval = kBaseFireIntervalFrames;
  static constexpr int kInitialShield = kInitialShieldLives;
  static constexpr int kMaxShield = kMaxShieldLives;
  static constexpr int kMaxHitTimer = kDamageImmunityFrames;
  static constexpr float kFireBoostPercent = kFireRateBoostPercent;
  static constexpr float kHitboxScale = kCollisionHitboxScale;
}

// ==============================================================================
// 4. Extraterrestrial Fleet & Formation Kinetics
// ==============================================================================
namespace Fleet {
  static constexpr float kAlienBaseWidthPixels = 84.0f;
  static constexpr float kAlienBaseHeightPixels = 84.0f;
  static constexpr float kAlienHorizontalSpacingPixels = 93.0f;
  static constexpr float kAlienVerticalSpacingPixels = 95.0f;
  static constexpr float kFleetBaseCruiseYPixels = 82.0f;

  static constexpr int kRandomWanderersCount = 4;
  static constexpr int kMinVectorMissilesPerStage = 8;
  static constexpr int kMaxVectorMissilesPerStage = 20;
  static constexpr float kMaxDeflectionAngleDeg = 15.0f;
  static constexpr int kSafeEvasionCorridorPixels = 118;
  static constexpr float kKamikazeBlastRadiusMultiplier = 1.25f; // 1.25x alien width
  static constexpr int kAlienSpinAnimationFrames = 46;

  static constexpr float kStage1MinSpeed = 6.0f;
  static constexpr float kStage1MaxSpeed = 9.0f;
  static constexpr float kStage2MinSpeed = 10.0f;
  static constexpr float kStage2MaxSpeed = 13.0f;
  static constexpr float kStage3MinSpeed = 14.0f;
  static constexpr float kStage3MaxSpeed = 16.5f;

  static constexpr int kMissileTurnCooldownMin = 35;
  static constexpr int kMissileTurnCooldownMax = 79;

  [[nodiscard]] inline constexpr float ComputeSpeed(int level, int max_levels) noexcept {
    const int stage = Progression::WaveToStage(level);
    const int wave_in_stage = Progression::WaveInStage(level);
    const float intra = static_cast<float>(wave_in_stage) / static_cast<float>(std::max(1, max_levels));

    if (stage == 1) {
      return kStage1MinSpeed + intra * (kStage1MaxSpeed - kStage1MinSpeed);
    } else if (stage == 2) {
      return kStage2MinSpeed + intra * (kStage2MaxSpeed - kStage2MinSpeed);
    } else {
      const float stage_add = std::min(2.0f, static_cast<float>(stage - 3) * 0.6f);
      return std::min(14.0f, kStage3MinSpeed + intra * 2.5f + stage_add);
    }
  }

  [[nodiscard]] inline constexpr int GetMaxAttackWaitFrames(int level) noexcept {
    const int stage = Progression::WaveToStage(level);
    if (stage == 1) return 5 * 60; // 300 frames (5.0s)
    if (stage == 2) return 4 * 60; // 240 frames (4.0s)
    if (stage == 3) return 3 * 60; // 180 frames (3.0s)
    return 2 * 60;                 // 120 frames (2.0s)
  }

  [[nodiscard]] inline constexpr int GetWandererMaxWaitFrames() noexcept {
    return 2 * 60; // 120 frames (2.0s max)
  }

  [[nodiscard]] inline constexpr int GetKamikazeQuota(int level) noexcept {
    const int stage = Progression::WaveToStage(level);
    if (stage == 1) return 1;
    if (stage == 2) return 2;
    return 3;
  }

  // Aliases
  static constexpr float kAlienBaseWidth = kAlienBaseWidthPixels;
  static constexpr float kAlienBaseHeight = kAlienBaseHeightPixels;
  static constexpr float kAlienBaseHSpacing = kAlienHorizontalSpacingPixels;
  static constexpr float kAlienBaseVSpacing = kAlienVerticalSpacingPixels;
  static constexpr float kAlienBaseCruiseY = kFleetBaseCruiseYPixels;
  static constexpr int kMinVectorMissiles = kMinVectorMissilesPerStage;
  static constexpr int kMaxVectorMissiles = kMaxVectorMissilesPerStage;
  static constexpr int kSafeCorridorPx = kSafeEvasionCorridorPixels;
  static constexpr int kSafeCorridorBasePx = kSafeEvasionCorridorPixels;
  static constexpr float kKamikazeLethalRadiusFactor = kKamikazeBlastRadiusMultiplier;
  static constexpr int kSpinFrames = kAlienSpinAnimationFrames;
}

// ==============================================================================
// 5. Munitions, Scoring, Capacities & Loot Weights
// ==============================================================================
namespace Combat {
  static constexpr size_t kMaxProjectiles = 512;
  static constexpr size_t kMaxExhaustParticles = 128;
  static constexpr size_t kMaxBonuses = 64;
  static constexpr size_t kMaxExplosionParticles = 4096;

  static constexpr int kAlienFireCooldownFrames = 8;
  static constexpr int kMissileTurnAnimationFrames = 20;

  static constexpr uint64_t kScorePerAlienHit = 10;
  static constexpr uint64_t kScorePerAlienNuked = 100;
  static constexpr uint64_t kExtraLifeScoreStep = 1000;

  static constexpr int kBonusMinWaitEnemies = 5;
  static constexpr int kBonusMaxWaitEnemies = 10;
  static constexpr double kBonusWaveDropProbability = 0.50; // 50% chance por onda
  static constexpr int kBonusWeightFire = 25;
  static constexpr int kBonusWeightMulti = 25;
  static constexpr int kBonusWeightSpeed = 25;
  static constexpr int kBonusWeightShield = 15;
  static constexpr int kBonusWeightNuke = 10;

  static constexpr float kMinBombSeparationDistPx = 68.0f;
  static constexpr int kExhaustParticleMinLife = 7;
  static constexpr int kExhaustParticleMaxLife = 10;

  // Aliases
  static constexpr int kAlienFireCooldown = kAlienFireCooldownFrames;
  static constexpr int kTurnTotalFrames = kMissileTurnAnimationFrames;
  static constexpr uint64_t kScoreAlienHit = kScorePerAlienHit;
  static constexpr uint64_t kScoreAlienNuked = kScorePerAlienNuked;
  static constexpr uint64_t kBaseExtraLifeScoreStep = kExtraLifeScoreStep;
  static constexpr int kBonusMinWait = kBonusMinWaitEnemies;
  static constexpr int kBonusMaxWait = kBonusMaxWaitEnemies;
  static constexpr double kBonusDropRate = kBonusWaveDropProbability;
}

// ==============================================================================
// 6. UI & State Durations
// ==============================================================================
namespace UI {
  static constexpr float kDebriefingDurationSec = 6.0f;
  static constexpr float kSupernovaDurationSec = 3.0f;
  static constexpr float kPlayerDeathAnimationSec = 1.35f;
  static constexpr float kDetailsOsdDurationSec = 1.5f;
  static constexpr float kMenuPageCycleDurationSec = 7.0f;
}

// ==============================================================================
// 7. Visual Presentation & Floating HUD Text (-30% Reduction)
// ==============================================================================
namespace Visuals {
  static constexpr float kFloatingTextHitFontSize = 24.0f;        // (+10 / +10 RADIUM)
  static constexpr float kFloatingTextNukeHitFontSize = 25.0f;    // (+100)
  static constexpr float kFloatingTextPowerupFontSize = 35.0f;    // (SPEED, FIRE, MULTI)
  static constexpr float kFloatingTextShieldFontSize = 36.0f;     // (1-UP SHIELD!)
  static constexpr float kFloatingTextNukeBannerFontSize = 42.0f; // (TACTICAL NUKE!)
  static constexpr float kFloatingTextNoticeFontSize = 22.0f;     // (Ship notices/cheats)

  // Aliases
  static constexpr float kTextHitSize = kFloatingTextHitFontSize;
  static constexpr float kTextNukeHitSize = kFloatingTextNukeHitFontSize;
  static constexpr float kTextPowerupSize = kFloatingTextPowerupFontSize;
  static constexpr float kTextShieldSize = kFloatingTextShieldFontSize;
  static constexpr float kTextNukeBannerSize = kFloatingTextNukeBannerFontSize;
  static constexpr float kTextNoticeSize = kFloatingTextNoticeFontSize;
}

// ==============================================================================
// 8. Audio Synthesis & Master Mix Parameters
// ==============================================================================
namespace Audio {
  static constexpr int kSampleRateHz = 44100;
  static constexpr size_t kMaxAudioVoices = 32;
  static constexpr size_t kAudioRingBufferCapacity = 128;
  static constexpr float kSoftLimitKnee = 0.70f;

  static constexpr float kLaserDurationSec = 0.50f;
  static constexpr float kAlienPopLightDurationSec = 0.50f;
  static constexpr float kAlienPopMediumDurationSec = 0.34f;
  static constexpr float kAlienPopHeavyDurationSec = 0.70f;
  static constexpr float kKamikazeAlertDurationSec = 0.30f;
  static constexpr float kKamikazeExplosionDurationSec = 0.88f;
  static constexpr float kPlayerHitDurationSec = 0.32f;
  static constexpr float kPlayerDestructionDurationSec = 1.60f;
  static constexpr float kNukeBlastDurationSec = 2.20f;
  static constexpr float kPowerupFireDurationSec = 0.22f;
  static constexpr float kPowerupMultiDurationSec = 0.28f;
  static constexpr float kPowerupSpeedDurationSec = 0.22f;
  static constexpr float kExtraLifeDurationSec = 0.62f;
  static constexpr float kGameOverDurationSec = 2.63f;

  // Aliases didáticos e retrocompatíveis completos
  static constexpr int kSampleRate = kSampleRateHz;
  static constexpr size_t kMaxVoices = kMaxAudioVoices;
  static constexpr size_t kRingBufferCapacity = kAudioRingBufferCapacity;
  static constexpr float kLaserDuration = kLaserDurationSec;
  static constexpr float kAlienPopLightDuration = kAlienPopLightDurationSec;
  static constexpr float kAlienPopMediumDuration = kAlienPopMediumDurationSec;
  static constexpr float kAlienPopHeavyDuration = kAlienPopHeavyDurationSec;
  static constexpr float kKamikazeAlertDuration = kKamikazeAlertDurationSec;
  static constexpr float kKamikazeExplodeDuration = kKamikazeExplosionDurationSec;
  static constexpr float kKamikazeExplosionDuration = kKamikazeExplosionDurationSec;
  static constexpr float kPlayerHitDuration = kPlayerHitDurationSec;
  static constexpr float kPlayerDestructionDuration = kPlayerDestructionDurationSec;
  static constexpr float kNukeBlastDuration = kNukeBlastDurationSec;
  static constexpr float kPowerupFireDuration = kPowerupFireDurationSec;
  static constexpr float kPowerupMultiDuration = kPowerupMultiDurationSec;
  static constexpr float kPowerupSpeedDuration = kPowerupSpeedDurationSec;
  static constexpr float kExtraLifeDuration = kExtraLifeDurationSec;
  static constexpr float kGameOverDuration = kGameOverDurationSec;

  static constexpr float kLaserSec = kLaserDurationSec;
  static constexpr float kPopLightSec = kAlienPopLightDurationSec;
  static constexpr float kPopMediumSec = kAlienPopMediumDurationSec;
  static constexpr float kPopHeavySec = kAlienPopHeavyDurationSec;
  static constexpr float kKamiAlertSec = kKamikazeAlertDurationSec;
  static constexpr float kKamiExplodeSec = kKamikazeExplosionDurationSec;
  static constexpr float kPlayerHitSec = kPlayerHitDurationSec;
  static constexpr float kPlayerDeathSec = kPlayerDestructionDurationSec;
  static constexpr float kNukeBlastSec = kNukeBlastDurationSec;
  static constexpr float kPowerupFireSec = kPowerupFireDurationSec;
  static constexpr float kPowerupMultiSec = kPowerupMultiDurationSec;
  static constexpr float kPowerupSpeedSec = kPowerupSpeedDurationSec;
  static constexpr float kExtraLifeSec = kExtraLifeDurationSec;
  static constexpr float kGameOverSec = kGameOverDurationSec;
}

// ==============================================================================
// 9. Cosmic Background & Starfield
// ==============================================================================
namespace Starfield {
  static constexpr double kBaseResolutionWidthPixels = 1280.0;
  static constexpr double kBaseResolutionHeightPixels = 720.0;
  static constexpr double kBaseStarCount = 240.0;
  static constexpr size_t kMinStarsCount = 180;
  static constexpr size_t kMaxStarsCount = 850;
  static constexpr float kForegroundLayerThreshold = 0.82f;

  // Aliases
  static constexpr double kBaseWidth = kBaseResolutionWidthPixels;
  static constexpr double kBaseHeight = kBaseResolutionHeightPixels;
  static constexpr double kBaseResolutionWidth = kBaseResolutionWidthPixels;
  static constexpr double kBaseResolutionHeight = kBaseResolutionHeightPixels;
  static constexpr size_t kMinStars = kMinStarsCount;
  static constexpr size_t kMaxStars = kMaxStarsCount;
  static constexpr float kForegroundThreshold = kForegroundLayerThreshold;
}

// ==============================================================================
// API de Topo Retrocompatível
// ==============================================================================
static constexpr float kPlayerBaseSpeed = Player::kBaseSpeed;
static constexpr float kPlayerBaseBulletSpeed = Player::kBaseBulletSpeed;
static constexpr float kPlayerBaseFireInterval = Player::kBaseFireInterval;

static constexpr int kPlayerMaxSpeedLevel = Player::kMaxSpeedLevel;
static constexpr int kPlayerMaxFireLevel = Player::kMaxFireLevel;
static constexpr int kPlayerMaxMultiShots = Player::kMaxMultiShots;
static constexpr int kPlayerMaxShield = Player::kMaxShield;
static constexpr int kPlayerShieldGateThreshold = Player::kShieldGateThreshold;

static constexpr int kMinVectorMissiles = Fleet::kMinVectorMissiles;
static constexpr int kMaxVectorMissiles = Fleet::kMaxVectorMissiles;
static constexpr float kMaxDeflectionAngleDeg = Fleet::kMaxDeflectionAngleDeg;
static constexpr int kSafeCorridorBasePx = Fleet::kSafeCorridorPx;

[[nodiscard]] inline constexpr float ComputePlayerSpeed(int speed_level) noexcept {
  return Player::ComputeSpeed(speed_level);
}
[[nodiscard]] inline constexpr int ComputeFireInterval(int fire_level) noexcept {
  return Player::ComputeFireInterval(fire_level);
}
[[nodiscard]] inline constexpr float ComputeAlienSpeed(int level, int max_levels) noexcept {
  return Fleet::ComputeSpeed(level, max_levels);
}
[[nodiscard]] inline constexpr int GetMaxAttackWaitFrames(int level) noexcept {
  return Fleet::GetMaxAttackWaitFrames(level);
}
[[nodiscard]] inline constexpr int GetWandererMaxWaitFrames() noexcept {
  return Fleet::GetWandererMaxWaitFrames();
}
[[nodiscard]] inline constexpr int GetKamikazeQuota(int level) noexcept {
  return Fleet::GetKamikazeQuota(level);
}

} // namespace GameRules

#endif // GAME_RULES_H
