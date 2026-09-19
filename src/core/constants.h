#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

/**
 * @namespace GameRules
 * @brief Canonical Single Source of Truth (SSOT) for gameplay tuning, physics, and fleet geometry.
 *
 * Architecture Principles:
 * - Pure C++20 standard header: zero graphical, platform, or external dependencies.
 * - Deep DRY: No alias constants, no cross-namespace pointer indirections, no forwarding wrappers.
 * - Autonomous Domains: Each namespace directly owns its parameters without artificial intermediate layers.
 * - Zero-allocation cached integer metrics for hot combat and rendering loops.
 */
namespace GameRules {

// =============================================================================
// 1. Simulation: Rigid 60 Hz deterministic clock, frame bounds, and master seed
// =============================================================================
// Controls the rigid fixed-step physics engine. Decouples simulation updates from
// display refresh rates to guarantee identical gameplay on 60Hz, 144Hz, or 360Hz monitors.
namespace Simulation {
  // Logic simulation frequency in Hertz (rigid clock decoupled from monitor refresh rates)
  static constexpr float    kSimulationFrequencyHz     = 60.0f;
  // Standard fixed physics delta time in seconds (1/60 s)
  static constexpr double   kFixedStepDurationSec      = 1.0 / static_cast<double>(kSimulationFrequencyHz);
  // Fixed step duration in integer nanoseconds (16.666 ms)
  static constexpr uint64_t kFixedStepNanoseconds      = 16'666'667ULL;
  // Maximum delta time clamp preventing catch-up death spirals during OS compositor stalls (250 ms)
  static constexpr uint64_t kMaxFrameDeltaNanoseconds  = 250'000'000ULL;
  // Maximum accumulator debt ceiling bounding physics backlog to at most 2 steps (33.3 ms)
  static constexpr uint64_t kMaxAccumulatorNanoseconds = 33'333'334ULL;
  // Master PRNG seed ensuring deterministic simulation, test reproducibility, and replays
  static constexpr uint32_t kDefaultSimulationSeed     = 0x1337BEEFu;
}

// =============================================================================
// 2. Stage Progression: Campaign structure, fanfare display, and warp travel
// =============================================================================
// Manages wave-to-stage progression logic and intermission timings between armadas.
namespace Progression {
  // Total combat waves composing a single stage cycle before difficulty re-tiering
  static constexpr int   kWavesPerStage                 = 15;
  // Duration in seconds for the stage-cleared musical fanfare display
  static constexpr float kStageFanfareDurationSeconds   = 6.0f;
  // Duration in seconds of the relativistic hyperspace star-streaming transition
  static constexpr float kHyperspaceWarpDurationSec     = 6.0f;
  // Parallax velocity multiplier applied to background stars during hyperspace travel
  static constexpr float kHyperspaceWarpSpeedMultiplier = 9.0f;

  // Converts a 1-based global wave index to its corresponding stage cycle
  [[nodiscard]] inline constexpr int WaveToStage(int wave) noexcept {
    return ((wave - 1) / kWavesPerStage) + 1;
  }

  // Converts a 1-based global wave index to its 0-based offset inside the current stage
  [[nodiscard]] inline constexpr int WaveInStage(int wave) noexcept {
    return (wave - 1) % kWavesPerStage;
  }
}

// =============================================================================
// 3. Player: Interceptor kinetics, weapon fire intervals, and shield health
// =============================================================================
// Defines movement agility, cannon fire rates, invulnerability windows, and hull lives.
namespace Player {
  // Baseline horizontal maneuverability speed in pixels per 60 Hz frame
  static constexpr float kPlayerBaseSpeed           = 3.5f;
  // Baseline primary plasma cannon projectile velocity (negative for upward trajectory)
  static constexpr float kPlayerBaseBulletSpeed     = -8.0f;
  // Baseline cooldown interval between primary plasma shots in simulation frames
  static constexpr float kBaseFireIntervalFrames    = 40.0f;
  // Hard floor limiting minimum fire cooldown to prevent projectile pool saturation
  static constexpr int   kMinFireIntervalFrames     = 8;

  // Starting shield lives awarded at match launch
  static constexpr int   kInitialShieldLives        = 3;
  // Maximum attainable shield capacity through supply pods
  static constexpr int   kPlayerMaxShield           = 8;
  // Shield threshold under which supply drops prioritize defensive matrix pods
  static constexpr int   kPlayerShieldGateThreshold = 4;

  // Maximum upgrade tier for agility thrusters
  static constexpr int   kPlayerMaxSpeedLevel       = 2;
  // Maximum upgrade tier for primary cannon cyclic rate
  static constexpr int   kPlayerMaxFireLevel        = 2;
  // Maximum number of simultaneous spread shot plasma cannons
  static constexpr int   kPlayerMaxMultiShots       = 3;

  // Invulnerability window duration in frames after taking damage
  static constexpr int   kDamageImmunityFrames      = 42;
  // Agility multiplier added per speed boost tier (+10%)
  static constexpr float kSpeedBoostPercent         = 0.10f;
  // Cyclic fire rate multiplier added per fire upgrade tier (+10%)
  static constexpr float kFireRateBoostPercent      = 0.10f;
  // Fair-play collision hitbox scale relative to visual sprite geometry
  static constexpr float kCollisionHitboxScale      = 0.65f;

  // Computes player horizontal velocity based on thruster upgrade tier
  [[nodiscard]] inline constexpr float ComputeSpeed(int speed_level) noexcept {
    const int clamped = std::clamp(speed_level, 0, kPlayerMaxSpeedLevel);
    return kPlayerBaseSpeed * (1.0f + kSpeedBoostPercent * static_cast<float>(clamped));
  }

  // Computes primary cannon firing interval in simulation frames based on fire upgrade tier
  [[nodiscard]] inline constexpr int ComputeFireInterval(int fire_level) noexcept {
    const int   clamped   = std::clamp(fire_level, 0, kPlayerMaxFireLevel);
    const float rate_mult = 1.0f + kFireRateBoostPercent * static_cast<float>(clamped);
    return std::max(kMinFireIntervalFrames, static_cast<int>(std::round(kBaseFireIntervalFrames / rate_mult)));
  }
}

// =============================================================================
// 4. Fleet: Alien armada formation geometry, movement speeds, and attack pacing
// =============================================================================
// Centralizes all alien metrics, flight parameters, cruise horizon, and attack timers.
// Includes the resolution-scaled integer metrics cache for O(1) hot-loop access.
namespace Fleet {
  // Base dimensions of alien ships in unscaled 720p coordinates (pixels)
  static constexpr float kAlienBaseWidthPixels          = 84.0f;
  static constexpr float kAlienBaseHeightPixels         = 84.0f;
  // Formation grid layout horizontal spacing between alien centers
  static constexpr float kAlienHorizontalSpacingPixels  = 93.0f;
  // Formation grid layout vertical spacing between alien centers
  static constexpr float kAlienVerticalSpacingPixels    = 95.0f;
  // Vertical cruise horizon line for the armada formation
  static constexpr float kFleetBaseCruiseYPixels        = 82.0f;

  // Number of independent wandering aliens joining the armada in Stage 2+
  static constexpr int   kRandomWanderersCount          = 4;
  // Range of smart turning missiles deployed per combat stage
  static constexpr int   kMinVectorMissilesPerStage     = 10;
  static constexpr int   kMaxVectorMissilesPerStage     = 20;
  // Maximum trajectory deflection angle for vector-guided missiles (degrees)
  static constexpr float kMaxDeflectionAngleDeg         = 15.0f;
  // Minimum guaranteed evasion corridor preventing converging cross-fire scissor traps (pixels)
  static constexpr int   kSafeEvasionCorridorPixels     = 118;
  // Proximity breach radius multiplier relative to alien width triggering kamikaze detonation
  static constexpr float kKamikazeBlastRadiusMultiplier = 1.25f;
  // Total frames composing an aerobatic alien spin animation
  static constexpr int   kAlienSpinAnimationFrames      = 46;
  // Taxa máxima angular de rotação de mira por frame (graus)
  static constexpr float kMaxTurnRateDeg                = 5.5f;
  // Fator de amortecimento inercial da trajetória vetorial
  static constexpr float kTurnDampingFactor             = 0.14f;

  // Stage progression speed boundaries (smoothly interpolated per wave)
  static constexpr float kStage1MinSpeed                = 6.0f;
  static constexpr float kStage1MaxSpeed                = 9.0f;
  static constexpr float kStage2MinSpeed                = 10.0f;
  static constexpr float kStage2MaxSpeed                = 13.0f;
  static constexpr float kStage3MinSpeed                = 14.0f;
  static constexpr float kStage3MaxSpeed                = 16.5f;

  // Attack dive delay ceilings in 60 Hz frames: Stage 1 (5s), Stage 2 (4s), Stage 3+ & Wanderers (3s)
  static constexpr int   kStage1MaxAttackWaitFrames     = 300;
  static constexpr int   kStage2MaxAttackWaitFrames     = 240;
  static constexpr int   kStage3MaxAttackWaitFrames     = 180;
  static constexpr int   kWandererMaxWaitFrames         = 180;

  // Computes current armada trajectory velocity interpolated across wave progression
  [[nodiscard]] inline constexpr float ComputeSpeed(int level, int max_levels) noexcept {
    const int   stage         = Progression::WaveToStage(level);
    const int   wave_in_stage = Progression::WaveInStage(level);
    const float intra         = static_cast<float>(wave_in_stage) / static_cast<float>(std::max(1, max_levels));
    if (stage == 1) return kStage1MinSpeed + intra * (kStage1MaxSpeed - kStage1MinSpeed);
    else if (stage == 2) return kStage2MinSpeed + intra * (kStage2MaxSpeed - kStage2MinSpeed);
    else return kStage3MinSpeed + intra * (kStage3MaxSpeed - kStage3MinSpeed);
  }

  // Returns maximum wait frames before a standard alien commences an attack dive
  [[nodiscard]] inline constexpr int GetMaxAttackWaitFrames(int level) noexcept {
    const int stage = Progression::WaveToStage(level);
    if (stage == 1) return kStage1MaxAttackWaitFrames;
    if (stage == 2) return kStage2MaxAttackWaitFrames;
    return kStage3MaxAttackWaitFrames;
  }

  // Returns maximum wait frames before an independent wandering alien commences an attack dive
  [[nodiscard]] inline constexpr int GetWandererMaxWaitFrames() noexcept {
    return kWandererMaxWaitFrames;
  }

  // Returns number of active kamikaze interceptors assigned per wave
  [[nodiscard]] inline constexpr int GetKamikazeQuota(int level) noexcept {
    return Progression::WaveToStage(level);
  }

  /**
   * @struct MetricsCache
   * @brief High-performance integer layout cache scaled to current window resolution.
   * Eliminates floating-point arithmetic and std::round from hot gameplay loops.
   */
  struct MetricsCache {
    int   width{static_cast<int>(kAlienBaseWidthPixels)};
    int   height{static_cast<int>(kAlienBaseHeightPixels)};
    int   h_spacing{static_cast<int>(kAlienHorizontalSpacingPixels)};
    int   v_spacing{static_cast<int>(kAlienVerticalSpacingPixels)};
    int   base_cruise_y{static_cast<int>(kFleetBaseCruiseYPixels)};
    float scale{1.0f};
  };

  // Retrieves the singleton metrics cache container
  inline MetricsCache& GetCache() noexcept {
    static MetricsCache cache{};
    return cache;
  }

  // Recalculates scaled integer metrics based on the display scale factor (called on window resize only)
  inline void InvalidateMetrics(float scale) noexcept {
    auto& c = GetCache();
    c.width         = static_cast<int>(std::round(kAlienBaseWidthPixels * scale));
    c.height        = static_cast<int>(std::round(kAlienBaseHeightPixels * scale));
    c.h_spacing     = static_cast<int>(std::round(kAlienHorizontalSpacingPixels * scale));
    c.v_spacing     = static_cast<int>(std::round(kAlienVerticalSpacingPixels * scale));
    c.base_cruise_y = static_cast<int>(std::round(kFleetBaseCruiseYPixels * scale));
    c.scale         = scale;
  }

  [[nodiscard]] inline int Width() noexcept       { return GetCache().width; }
  [[nodiscard]] inline int Height() noexcept      { return GetCache().height; }
  [[nodiscard]] inline int HSpacing() noexcept    { return GetCache().h_spacing; }
  [[nodiscard]] inline int VSpacing() noexcept    { return GetCache().v_spacing; }
  [[nodiscard]] inline int BaseCruiseY() noexcept { return GetCache().base_cruise_y; }
}

// =============================================================================
// 5. Combat: Object pools, scoring values, and supply drop probabilities
// =============================================================================
// Configures static entity pool capacities, enemy firing intervals, and score rewards.
namespace Combat {
  // Static array capacities for zero-heap-allocation entity pools
  static constexpr size_t  kMaxProjectiles            = 512;
  static constexpr size_t  kMaxExhaustParticles       = 128;
  static constexpr size_t  kMaxBonuses                = 64;
  static constexpr size_t  kMaxExplosionParticles     = 4096;

  // ===========================================================================
  // Explosion Debris Dynamics & Particle Volume
  // ===========================================================================
  // Absolute floor for spawned debris shards to guarantee a visible burst even on short-lived puffs
  static constexpr int     kExplosionDebrisMinParticles    = 20;
  // Hard upper ceiling for debris particles per explosion event to preserve memory bandwidth and pool headroom
  static constexpr int     kExplosionDebrisMaxParticles    = 200;
  // Sensitivity multiplier scaling particle volume according to explosion duration and user detail level
  static constexpr int     kExplosionDebrisScaleFactor     = 8;
  // Ratio of resolution-scaled alien width defining the maximum initial radial spread of bursting shrapnel
  static constexpr float   kExplosionDebrisRadiusMultiplier = 1.0f;

  // Mandatory cooldown frames between consecutive enemy weapon discharges
  static constexpr int     kAlienFireCooldown         = 8;
  // Total frames defining vector missile deflection turn duration
  static constexpr int     kTurnTotalFrames           = 18;

  // Score points awarded for eliminating a standard formation alien
  static constexpr uint64_t kScorePerAlienHit         = 10ULL;
  // Score points awarded for neutralizing an aggressive Kamikaze diver (+20 pts)
  static constexpr uint64_t kScorePerKamikazeAlienHit = 20ULL;
  // Score points awarded per enemy vessel vaporized by an atomic blast
  static constexpr uint64_t kScorePerAlienNuked       = 100ULL;
  // Score progress threshold required to earn an additional shield life (1-UP)
  static constexpr uint64_t kExtraLifeScoreStep       = 1000ULL;

  // Baseline probability of spawning a supply pod upon eliminating a convoy (50%)
  static constexpr double  kBonusWaveDropProbability  = 0.50;

  // Relative loot table probability weights for supply drop items
  static constexpr int     kBonusWeightFire           = 25;
  static constexpr int     kBonusWeightMulti          = 25;
  static constexpr int     kBonusWeightSpeed          = 25;
  static constexpr int     kBonusWeightShield         = 15;
  static constexpr int     kBonusWeightNuke           = 10;
  static constexpr int     kTotalBonusWeight =
      kBonusWeightFire + kBonusWeightMulti + kBonusWeightSpeed + kBonusWeightShield + kBonusWeightNuke;
}

// =============================================================================
// 6. Visuals: Floating HUD typography point sizes
// =============================================================================
// Defines baseline typographic point sizes for in-game floating score and notification banners.
namespace Visuals {
  static constexpr float kFloatingTextHitFontSize        = 24.0f;
  static constexpr float kFloatingTextNukeHitFontSize    = 25.0f;
  static constexpr float kFloatingTextPowerupFontSize    = 35.0f;
  static constexpr float kFloatingTextShieldFontSize     = 36.0f;
  static constexpr float kFloatingTextNukeBannerFontSize = 42.0f;
  static constexpr float kFloatingTextNoticeFontSize     = 22.0f;

  // Procedural screen shake trauma dynamics (Trauma^2 with linear frame decay)
  static constexpr float kScreenShakeMaxTrauma           = 1.0f;
  static constexpr float kScreenShakeTraumaDecay         = 0.040f;
}

// =============================================================================
// 7. Audio: Procedural physical synthesizer constants and voice budgets
// =============================================================================
// Parameters for real-time procedural audio synthesis in RAM (CD Quality Stereo 2.1).
namespace Audio {
  // Synthesizer sampling rate in Hertz (CD Quality Stereo 2.1)
  static constexpr int    kSampleRateHz                 = 44100;
  // Maximum concurrent polyphonic synthesizer voice slots
  static constexpr size_t kMaxAudioVoices               = 32;
  // Lock-free ring buffer command queue capacity (must be power of two)
  static constexpr size_t kAudioRingBufferCapacity      = 128;
  // Hyperbolic tangent soft-saturation knee preventing digital clipping
  static constexpr float  kSoftLimitKnee                = 0.70f;

  // Procedural audio envelope durations in seconds
  static constexpr float  kLaserDurationSec             = 0.50f;
  static constexpr float  kAlienPopLightDurationSec     = 0.55f;
  static constexpr float  kAlienPopMediumDurationSec    = 0.60f;
  static constexpr float  kAlienPopHeavyDurationSec     = 0.70f;
  static constexpr float  kKamikazeAlertDurationSec     = 0.30f;
  static constexpr float  kKamikazeExplosionDurationSec = 0.90f;
  static constexpr float  kPlayerHitDurationSec         = 0.32f;
  static constexpr float  kPlayerDestructionDurationSec = 1.60f;
  static constexpr float  kNukeBlastDurationSec         = 2.20f;
  static constexpr float  kPowerupFireDurationSec       = 0.22f;
  static constexpr float  kPowerupMultiDurationSec      = 0.28f;
  static constexpr float  kPowerupSpeedDurationSec      = 0.22f;
  static constexpr float  kExtraLifeDurationSec         = 0.62f;
}

// =============================================================================
// 8. Starfield: Cosmic background density, reference bounds, and parallax layers
// =============================================================================
// Controls procedural star distribution, density scaling, and foreground parallax splitting.
namespace Starfield {
  // Reference resolution width for square-root surface area star density scaling (pixels)
  static constexpr double kBaseResolutionWidthPixels   = 1280.0;
  // Reference resolution height for square-root surface area star density scaling (pixels)
  static constexpr double kBaseResolutionHeightPixels  = 720.0;
  // Nominal star count generated at baseline reference resolution
  static constexpr double kBaseStarCount               = 240.0;
  // Hard minimum star count ceiling preventing starfield starvation
  static constexpr size_t kMinStarsCount               = 180;
  // Hard maximum star count ceiling preventing GPU fillrate saturation
  static constexpr size_t kMaxStarsCount               = 850;
  // Parallax threshold splitting distant background points from luminous foreground stars
  static constexpr float  kForegroundLayerThreshold    = 0.82f;

  // Fixed-point twinkle phase: 16-bit phase, top 8 bits address the lookup table.
  static constexpr uint32_t kTwinklePhaseScale = 65536u;
  static constexpr uint32_t kTwinkleTableSize = 256u;

  // Calibrated star density scaling factors per detail level (0 = OFF, 4 = MAX)
  static constexpr double kDensityFactorLevel0         = 0.00; // Deep black space (disabled)
  static constexpr double kDensityFactorLevel1         = 0.50; // Classic arcade starfield
  static constexpr double kDensityFactorLevel2         = 1.50; // Dense cinematic baseline
  static constexpr double kDensityFactorLevel3         = 2.00; // Balanced deep-space nebula
  static constexpr double kDensityFactorLevel4         = 2.50; // Hyper-dense immersive field
  static constexpr double kDefaultDensityFactor        = kDensityFactorLevel2;

  // Pure function mapping user detail levels (0..4) to starfield density factors
  [[nodiscard]] inline constexpr double GetDensityFactor(int level) noexcept {
    switch (level) {
      case 0:  return kDensityFactorLevel0;
      case 1:  return kDensityFactorLevel1;
      case 2:  return kDensityFactorLevel2;
      case 3:  return kDensityFactorLevel3;
      case 4:  return kDensityFactorLevel4;
      default: return kDefaultDensityFactor;
    }
  }
}

// =============================================================================
// 9. Special Entities: Quantum orbital parameters for Alien 4 (Marie Curielien)
// =============================================================================
// Defines the perspective-inclined electrosphere, quantum particle counts, and rotation velocities.
namespace SpecialEntities {
  // Number of pre-computed trigonometric knots forming the orbital ellipse geometry
  static constexpr size_t kAlien4OrbitKnotSteps           = 36;
  // Number of quantum particles orbiting inside the electrosphere
  static constexpr int    kAlien4ElectronCount            = 2;
  // Angular velocity of electron orbit in radians per second
  static constexpr float  kAlien4ElectronAngularSpeed     = 3.0f;
  // Orbital major axis ellipse scale relative to vessel aura
  static constexpr float  kAlien4ElectronRadiusScale      = 0.90f;
  // Orbital minor axis ellipse scale (inclined perspective projection)
  static constexpr float  kAlien4ElectronMinorRadiusScale = 0.40f;
  // Physical diameter of the electron core in pixels
  static constexpr float  kAlien4ElectronRadiusPixels     = 2.5f;
  // Radius of the glowing quantum aura surrounding orbiting electrons in pixels
  static constexpr float  kAlien4ElectronAuraRadiusPixels = 8.0f;
  // Orbital inclination plane angle in radians (~36 degrees)
  static constexpr float  kAlien4OrbitTiltRad             = 0.628f;
  // Toggles alternation between vessels with and without active electrospheres
  static constexpr bool   kAlien4AlternateElectrosphere   = true;
}

// =============================================================================
// 10. Telemetry: Hardware performance OSD monitor metrics
// =============================================================================
// Controls CPU, RAM, and FPS polling intervals and presentation typography.
namespace TelemetryConfig {
  // Polling rate interval for asynchronous hardware metric sampling (500 ms = 0.5 s)
  static constexpr uint64_t kSampleIntervalNs = 500'000'000ULL;
  // Typographic font point size for the telemetry OSD banner
  static constexpr float    kFontSize         = 21.6f;
}

} // namespace GameRules

#endif  // CONSTANTS_H
