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
 * - Deep DRY: No duplicate formulas, derived constants computed from base values, zero alias wrappers.
 * - Autonomous Domains: Each sub-namespace directly owns its parameters without artificial intermediate layers.
 * - Zero-allocation cached integer metrics for hot combat and rendering loops.
 */
namespace GameRules {

// =============================================================================
// 1. Simulation: Rigid 60 Hz deterministic clock, frame bounds, and master seed
// =============================================================================
// Controls the rigid fixed-step physics engine. Decouples simulation updates from
// display refresh rates to guarantee identical gameplay on 60Hz, 144Hz, or 360Hz monitors.
namespace Simulation {
  /// Logic simulation frequency in Hertz (rigid clock decoupled from monitor refresh rates)
  static constexpr float    kSimulationFrequencyHz     = 60.0f;
  /// Standard fixed physics delta time in seconds (1/60 s)
  static constexpr double   kFixedStepDurationSec      = 1.0 / static_cast<double>(kSimulationFrequencyHz);
  /// Fixed step duration in integer nanoseconds (16.666 ms)
  static constexpr uint64_t kFixedStepNanoseconds      = 16'666'667ULL;
  /// Maximum delta time clamp preventing catch-up death spirals during OS compositor stalls (250 ms)
  static constexpr uint64_t kMaxFrameDeltaNanoseconds  = 250'000'000ULL;
  /// Maximum accumulator debt ceiling bounding physics backlog to at most 2 steps (33.3 ms)
  static constexpr uint64_t kMaxAccumulatorNanoseconds = kFixedStepNanoseconds * 2ULL;
  /// Master PRNG seed ensuring deterministic simulation, test reproducibility, and replays
  static constexpr uint32_t kDefaultSimulationSeed     = 0x1337BEEFu;
}

// =============================================================================
// 2. Stage Progression: Campaign structure, fanfare display, and warp travel
// =============================================================================
// Manages wave-to-stage progression logic and intermission timings between armadas.
namespace Progression {
  /// Total combat waves composing a single stage cycle before difficulty re-tiering
  static constexpr int   kWavesPerStage                 = 15;
  /// Duration in seconds for the stage-cleared musical fanfare display and hyperspace star transition
  static constexpr float kStageFanfareDurationSeconds   = 6.0f;
  /// Delay in simulation frames before the incoming armada begins its atmospheric incursion (60 frames = 1.0 second @ 60 Hz)
  static constexpr int   kPostFanfareArmadaDelayFrames  = 60;
  /// Parallax velocity multiplier applied to background stars during hyperspace travel
  static constexpr float kHyperspaceWarpSpeedMultiplier = 9.0f;

  /// Converts a 1-based global wave index to its corresponding stage cycle
  [[nodiscard]] inline constexpr int WaveToStage(int wave) noexcept {
    return ((wave - 1) / kWavesPerStage) + 1;
  }

  /// Converts a 1-based global wave index to its 0-based offset inside the current stage
  [[nodiscard]] inline constexpr int WaveInStage(int wave) noexcept {
    return (wave - 1) % kWavesPerStage;
  }
}

// =============================================================================
// 3. Player: Interceptor kinetics, weapon fire intervals, and shield health
// =============================================================================
// Defines movement agility, cannon fire rates, invulnerability windows, and hull shields.
namespace Player {
  /// Baseline horizontal maneuverability speed in pixels per 60 Hz frame
  static constexpr float kPlayerBaseSpeed           = 3.5f;
  /// Baseline primary plasma cannon projectile velocity (negative for upward trajectory)
  static constexpr float kPlayerBaseBulletSpeed     = -8.0f;
  /// Baseline cooldown interval between primary plasma shots in simulation frames
  static constexpr float kBaseFireIntervalFrames    = 40.0f;
  /// Hard floor limiting minimum fire cooldown to prevent projectile pool saturation
  static constexpr int   kMinFireIntervalFrames     = 8;

  /// Starting shield capacity awarded at match launch
  static constexpr int   kInitialShields            = 3;
  /// Maximum attainable shield capacity through supply pods
  static constexpr int   kPlayerMaxShield           = 8;
  /// Shield threshold under which supply drops prioritize defensive protection pods
  static constexpr int   kPlayerShieldGateThreshold = 4;

  /// Maximum upgrade tier for agility thrusters
  static constexpr int   kPlayerMaxSpeedLevel       = 5;
  /// Maximum upgrade tier for primary cannon cyclic rate
  static constexpr int   kPlayerMaxFireLevel        = 2;
  /// Maximum number of simultaneous spread shot plasma cannons
  static constexpr int   kPlayerMaxMultiShots       = 3;

  /// Invulnerability window duration in frames after taking damage
  static constexpr int   kDamageImmunityFrames      = 42;
  /// Agility multiplier added per speed boost tier (+30%)
  static constexpr float kSpeedBoostPercent         = 0.30f;
  /// Cyclic fire rate multiplier added per fire upgrade tier (+10%)
  static constexpr float kFireRateBoostPercent      = 0.10f;
  /// Fair-play collision hitbox scale relative to visual sprite geometry
  static constexpr float kCollisionHitboxScale      = 0.65f;

  /// Computes player horizontal velocity based on thruster upgrade tier
  [[nodiscard]] inline constexpr float ComputeSpeed(int speed_level) noexcept {
    const int clamped = std::clamp(speed_level, 0, kPlayerMaxSpeedLevel);
    return kPlayerBaseSpeed * (1.0f + kSpeedBoostPercent * static_cast<float>(clamped));
  }

  /// Computes primary cannon firing interval in simulation frames based on fire upgrade tier
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
namespace Fleet {
  /// Base unscaled visual width of alien starships in 720p coordinates (pixels)
  static constexpr float kAlienBaseWidthPixels          = 84.0f;
  /// Base unscaled visual height of alien starships in 720p coordinates (pixels)
  static constexpr float kAlienBaseHeightPixels         = 84.0f;
  /// Formation grid layout horizontal spacing between alien centers
  static constexpr float kAlienHorizontalSpacingPixels  = 93.0f;
  /// Formation grid layout vertical spacing between alien centers
  static constexpr float kAlienVerticalSpacingPixels    = 95.0f;
  /// Vertical cruise horizon line for the armada formation
  static constexpr float kFleetBaseCruiseYPixels        = 82.0f;
  /// Absolute floor for fleet trajectory velocity in pixels per frame preventing stalled interpolation
  static constexpr float kFleetMinVelocityPixels        = 1.8f;
  /// Damping factor applied to trajectory velocity during attack dives (90%)
  static constexpr float kAttackDiveSpeedMultiplier     = 0.90f;
  /// Minimum velocity floor during attack dives in pixels per frame
  static constexpr float kAttackDiveMinVelocityPixels   = 1.5f;
  /// Safety margin in pixels added to starship half-width for screen edge containment
  static constexpr int   kFleetMarginXPixels            = 14;
  /// Minimum vertical top margin in pixels for fleet flight path
  static constexpr int   kFleetMarginYMinPixels         = 16;
  /// Reserved bottom margin in pixels protecting player evasion and defense line
  static constexpr int   kFleetMarginYMaxOffsetPixels   = 180;

  /// Number of independent wandering aliens joining the armada in Stage 2+
  static constexpr int   kRandomWanderersCount          = 4;
  /// Probability p (0.0 to 1.0) that an alien bomb deploys with vector-guided homing thrusters (Seeker Missile)
  static constexpr float kSeekerMissileProbability      = 0.25f;
  /// Maximum trajectory deflection angle for vector-guided missiles (degrees)
  static constexpr float kMaxDeflectionAngleDeg         = 15.0f;
  /// Minimum guaranteed evasion corridor preventing converging cross-fire scissor traps (pixels)
  static constexpr int   kSafeEvasionCorridorPixels     = 118;
  /// Proximity breach radius multiplier relative to alien width triggering kamikaze detonation
  static constexpr float kKamikazeBlastRadiusMultiplier = 1.4f;
  /// Total frames composing an aerobatic alien spin animation
  static constexpr int   kAlienSpinAnimationFrames      = 46;
  /// Maximum angular reticle tracking rate per 60 Hz frame (degrees)
  static constexpr float kMaxTurnRateDeg                = 5.5f;
  /// Inertial damping factor applied to trajectory velocity steering
  static constexpr float kTurnDampingFactor             = 0.14f;

  /// Compile-time Honeycomb geometry tuning
  static constexpr float kFormationRowStagger          = 0.50f;
  static constexpr int   kFormationStage2CurveCenter   = 4;
  static constexpr int   kFormationStage2CurvePlateau  = 3;
  static constexpr float kFormationStage3WaveDip       = 0.25f;
  static constexpr float kFormationStage3FlankDivisor  = 6.0f;

  /// Stage 1 minimum fleet trajectory velocity in pixels per frame
  static constexpr float kStage1MinSpeed                = 6.0f;
  /// Stage 1 maximum fleet trajectory velocity in pixels per frame
  static constexpr float kStage1MaxSpeed                = 9.0f;
  /// Stage 2 minimum fleet trajectory velocity in pixels per frame
  static constexpr float kStage2MinSpeed                = 10.0f;
  /// Stage 2 maximum fleet trajectory velocity in pixels per frame
  static constexpr float kStage2MaxSpeed                = 13.0f;
  /// Stage 3+ minimum fleet trajectory velocity in pixels per frame
  static constexpr float kStage3MinSpeed                = 14.0f;
  /// Stage 3+ maximum fleet trajectory velocity in pixels per frame
  static constexpr float kStage3MaxSpeed                = 17.0f;

  /// Attack dive delay ceiling in frames for Stage 1 (5 seconds @ 60 Hz)
  static constexpr int   kStage1MaxAttackWaitFrames     = 300;
  /// Attack dive delay ceiling in frames for Stage 2 (4 seconds @ 60 Hz)
  static constexpr int   kStage2MaxAttackWaitFrames     = 240;
  /// Attack dive delay ceiling in frames for Stage 3+ (3 seconds @ 60 Hz)
  static constexpr int   kStage3MaxAttackWaitFrames     = 180;
  /// Attack dive delay ceiling in frames for independent wandering aliens (3 seconds @ 60 Hz)
  static constexpr int   kWandererMaxWaitFrames         = 180;

  /// Computes current armada trajectory velocity interpolated across wave progression
  [[nodiscard]] inline constexpr float ComputeSpeed(int level, int max_levels) noexcept {
    const int   stage         = Progression::WaveToStage(level);
    const int   wave_in_stage = Progression::WaveInStage(level);
    const float intra         = static_cast<float>(wave_in_stage) / static_cast<float>(std::max(1, max_levels));
    if (stage == 1) return kStage1MinSpeed + intra * (kStage1MaxSpeed - kStage1MinSpeed);
    else if (stage == 2) return kStage2MinSpeed + intra * (kStage2MaxSpeed - kStage2MinSpeed);
    else return kStage3MinSpeed + intra * (kStage3MaxSpeed - kStage3MinSpeed);
  }

  /// Returns maximum wait frames before a standard alien commences an attack dive
  [[nodiscard]] inline constexpr int GetMaxAttackWaitFrames(int level) noexcept {
    const int stage = Progression::WaveToStage(level);
    if (stage == 1) return kStage1MaxAttackWaitFrames;
    if (stage == 2) return kStage2MaxAttackWaitFrames;
    return kStage3MaxAttackWaitFrames;
  }

  /// Returns maximum wait frames before an independent wandering alien commences an attack dive
  [[nodiscard]] inline constexpr int GetWandererMaxWaitFrames() noexcept {
    return kWandererMaxWaitFrames;
  }

  /// Returns number of active kamikaze interceptors assigned per wave
  [[nodiscard]] inline constexpr int GetKamikazeQuota(int level) noexcept {
    return Progression::WaveToStage(level);
  }
}

// =============================================================================
// 5. Combat: Object pools, scoring values, and supply drop probabilities
// =============================================================================
namespace Combat {
  /// Maximum concurrent player and alien projectiles allowed in flight
  static constexpr size_t  kMaxProjectiles                  = 512;
  /// Maximum concurrent engine exhaust particles in pool
  static constexpr size_t  kMaxExhaustParticles             = 128;
  /// Maximum active supply pods on screen
  static constexpr size_t  kMaxBonuses                      = 64;
  /// Maximum concurrent debris shards across all explosions
  static constexpr size_t  kMaxExplosionParticles           = 4096;
  /// Compile-time size of the explosion drag lookup table
  static constexpr size_t  kExplosionDecayTableSize         = 128;
  /// Initial particle spawn radius as a fraction of the starship radius
  static constexpr float   kExplosionInitialRadiusFactor   = 0.25f;
  /// Minimum randomized expansion fraction
  static constexpr float   kExplosionExpansionMinFactor    = 0.35f;
  /// Minimum useful decay-series denominator guard
  static constexpr float   kExplosionDecayEpsilon           = 0.10f;
  /// Maximum point-batch capacity reserved by the explosion renderer
  static constexpr size_t  kExplosionPointBatchCapacity     = 32768;

  /// Absolute floor for spawned debris shards guaranteeing visible burst
  static constexpr int     kExplosionDebrisMinParticles     = 20;
  /// Hard upper ceiling for debris particles per explosion event
  static constexpr int     kExplosionDebrisMaxParticles     = 200;
  /// Sensitivity multiplier scaling particle volume by user detail level
  static constexpr int     kExplosionDebrisScaleFactor      = 8;
  /// Ratio of starship width defining initial burst radius
  static constexpr float   kExplosionDebrisRadiusMultiplier = 1.0f;
  /// Geometric drag deceleration factor applied to debris velocity per frame
  static constexpr float   kExplosionDrag                   = 0.96f;

  /// Score points awarded for destroying a standard armada alien
  static constexpr uint64_t kScorePerAlienHit               = 10ULL;
  /// Score points awarded for neutralizing an aggressive Kamikaze diver (+20 pts)
  static constexpr uint64_t kScorePerKamikazeAlienHit       = 20ULL;
  /// Score points awarded per enemy starship vaporized by a Tactical Nuke
  static constexpr uint64_t kScorePerAlienNuked             = 50ULL;
  /// Milestone score progression required to earn an additional shield (Shield-UP)
  static constexpr uint64_t kExtraShieldScoreStep           = 1000ULL;

  /// Maximum distinct supply pod bonus drops allowed per 15-wave stage cycle (each type at most once)
  static constexpr int     kMaxBonusesPerStage              = 5;
  /// Baseline probability of spawning a supply pod upon eliminating a convoy (50%)
  static constexpr double  kBonusWaveDropProbability        = 0.50;
  /// Loot weight for Rapid Fire upgrade
  static constexpr int     kBonusWeightFire                 = 25;
  /// Loot weight for Multi-Cannon upgrade
  static constexpr int     kBonusWeightMulti                = 25;
  /// Loot weight for Speed Boost upgrade
  static constexpr int     kBonusWeightSpeed                = 25;
  /// Loot weight for Shield Protection restore
  static constexpr int     kBonusWeightShield               = 15;
  /// Loot weight for Tactical Nuke warhead
  static constexpr int     kBonusWeightNuke                 = 10;
  /// Initial spawn delay min frames after convoy elimination
  static constexpr int     kBonusInitialWaitMinFrames       = 5;
  /// Initial spawn delay max frames after convoy elimination
  static constexpr int     kBonusInitialWaitMaxFrames       = 10;

  /// Baseline bomb ceiling before stage progression scaling
  static constexpr int     kBaseBombsPerStage               = 4;
  /// Maximum concurrent bombs active on screen per stage
  static constexpr int     kMaxBombsPerStage                = 10;
  /// Wave divisor scaling maximum concurrent bomb quota
  static constexpr int     kBombsPerStageLevelDivisor       = 3;
  /// Base multiplier applied to fleet speed to derive bomb velocity
  static constexpr float   kBombSpeedBaseMultiplier         = 0.70f;
  /// Base additive velocity offset for bombs in pixels/frame
  static constexpr float   kBombSpeedBaseOffset             = 1.8f;
  /// Absolute floor for bomb travel speed in pixels/frame
  static constexpr float   kBombMinSpeed                    = 2.8f;
  /// Maximum horizontal-to-vertical speed ratio preventing horizontal-only trajectories
  static constexpr float   kBombMaxHorizontalSpeedRatio     = 0.32f;
  /// Horizontal speed scaling factor for angled bombs
  static constexpr float   kBombHorizontalSpeedMultiplier   = 0.40f;
  /// Individual bomb speed variance base percentage (84%)
  static constexpr float   kBombSpeedIndividualVarianceBase = 0.84f;
  /// Individual bomb speed variance random range (0% to 34%)
  static constexpr int     kBombSpeedIndividualVarianceRange = 34;
  /// Individual vector bomb speed variance base percentage (88%)
  static constexpr float   kBombSpeedVectorVarianceBase     = 0.88f;
  /// Individual vector bomb speed variance random range (0% to 29%)
  static constexpr int     kBombSpeedVectorVarianceRange    = 29;
  /// Minimum distance in pixels between simultaneous bombs to prevent clustering
  static constexpr float   kBombNearDistancePixels          = 68.0f;
  /// Base cycle multiplier for bomb speed scaling in Stage 3+
  static constexpr float   kCycleBombMultBase               = 1.40f;
  /// Maximum cycle multiplier ceiling for bomb speed
  static constexpr float   kCycleBombMultMax                = 1.65f;
  /// Multiplier increment added per stage cycle beyond Stage 3
  static constexpr float   kCycleBombMultIncrement          = 0.08f;
  /// Vertical offset of player from bottom of screen in unscaled pixels
  static constexpr int     kPlayerYOffsetPixels             = 86;

  /// Total frames defining vector missile deflection turn duration
  static constexpr int     kTurnTotalFrames                 = 18;
  /// Initial frame count of the vector missile turning animation timer
  static constexpr int     kTurningBombTurnTimerFrames      = 20;
  /// Rear offset ratio relative to missile height for exhaust ignition
  static constexpr float   kTurningBombRearOffsetRatio      = 0.44f;
  /// Lateral spread scale for turning missile thruster exhaust
  static constexpr float   kTurningBombLateralSpreadScale   = 3.6f;
  /// Minimum vertical travel percent (38%) before seeker deflection activates
  static constexpr int     kTurningTriggerYMinPercent       = 38;
  /// Maximum vertical travel percent (69%) before seeker deflection activates
  static constexpr int     kTurningTriggerYMaxPercent       = 69;
  /// Random angular deflection jitter in degrees (±2 deg)
  static constexpr int     kDeflectionJitterDeg             = 2;
  /// Maximum random deflection arc in degrees
  static constexpr float   kDeflectionRandomRangeDeg        = 30.0f;
  /// Minimum cooldown frames between consecutive smart turning seeker deployments
  static constexpr int     kTurningSpacingCooldownMinFrames = 35;
  /// Maximum cooldown frames between consecutive smart turning seeker deployments
  static constexpr int     kTurningSpacingCooldownMaxFrames = 79;
  /// Relativistic bomb horizontal oscillation amplitude (pixels)
  static constexpr float   kRelativisticBombOscAmplitude    = 2.2f;
  /// Relativistic bomb horizontal oscillation frequency (rad/frame)
  static constexpr float   kRelativisticBombOscFrequency    = 0.18f;
  /// Wave phase cycle ceiling in degrees for relativistic trajectory
  static constexpr int     kWavePhaseMaxDegrees             = 360;

  /// Baseline velocity of backward-ejected thruster exhaust particles
  static constexpr float   kExhaustSpeedBase                = 1.6f;
  /// Speed variance range for exhaust particles
  static constexpr int     kExhaustSpeedVarianceRange       = 99;
  /// Lateral spread range for missile exhaust ejection
  static constexpr int     kExhaustLateralSpreadRange       = 50;
  /// Random angular jitter range for exhaust spread
  static constexpr int     kExhaustSpreadJitterRange        = 20;
  /// Minimum particle lifetime in frames
  static constexpr int     kExhaustLifeMinFrames            = 7;
  /// Maximum particle lifetime in frames
  static constexpr int     kExhaustLifeMaxFrames            = 10;
  /// Number of distinct engine exhaust color palettes
  static constexpr int     kEngineTypeCount                 = 4;

  /// Mandatory cooldown frames between consecutive enemy weapon discharges
  static constexpr int     kAlienFireCooldown               = 8;
  /// Minimum fire probability percentage floor
  static constexpr int     kFireChanceMin                   = 20;
  /// Fire probability numerator factor scaled by fleet population
  static constexpr int     kFireChanceFleetFactor           = 50;
  /// Fire probability divisor for fleet population scaling
  static constexpr int     kFireChanceFleetDivisor          = 25;
  /// Total number of distinct alien ship textures available
  static constexpr int     kAlienTextureCount               = 15;
}

// =============================================================================
// 6. Visuals: Floating HUD typography point sizes and procedural visual FX
// =============================================================================
namespace Visuals {
  /// Font size for standard alien hit score indicator
  static constexpr float kFloatingTextHitFontSize        = 24.0f;
  /// Font size for Tactical Nuke vaporization hit score indicator
  static constexpr float kFloatingTextNukeHitFontSize    = 25.0f;
  /// Font size for power-up acquisition banner
  static constexpr float kFloatingTextPowerupFontSize    = 35.0f;
  /// Font size for shield protection restore notification
  static constexpr float kFloatingTextShieldFontSize     = 36.0f;
  /// Font size for screen-wide Tactical Nuke activation banner
  static constexpr float kFloatingTextNukeBannerFontSize = 42.0f;
  /// Font size for tactical notices and ship reconfiguration alerts
  static constexpr float kFloatingTextNoticeFontSize     = 22.0f;

  /// Maximum procedural screen shake trauma ceiling [0.0, 1.0]
  static constexpr float kScreenShakeMaxTrauma           = 1.0f;
  /// Linear trauma decay rate per 60 Hz frame
  static constexpr float kScreenShakeTraumaDecay         = 0.040f;
  /// Inactive menu auto-cycling timer in frames (~6.7 s @ 60 Hz)
  static constexpr int   kMenuAutoCycleFrames            = 400;

  /// Minimum pulse amplitude floor for Kamikaze warning aura breathing
  static constexpr float kPulseAmplitudeMin              = 0.40f;
  /// Maximum pulse amplitude ceiling for Kamikaze warning aura breathing
  static constexpr float kPulseAmplitudeMax              = 1.00f;
  /// Temporal breathing frequency for Kamikaze pulsating aura
  static constexpr float kKamikazePulseFrequency         = 1.5f;
  /// Deterministic 60 Hz phase step increment for Kamikaze aura
  static constexpr float kKamikazePulsePhaseStep         = 0.08f;
  /// Pre-calculated midpoint of Kamikaze breathing pulse (Deep DRY)
  static constexpr float kKamikazePulseMid               = (kPulseAmplitudeMax + kPulseAmplitudeMin) * 0.5f;
  /// Pre-calculated amplitude excursion of Kamikaze breathing pulse (Deep DRY)
  static constexpr float kKamikazePulseAmp               = (kPulseAmplitudeMax - kPulseAmplitudeMin) * 0.5f;

  /// Base radial scaling for Kamikaze warning rings relative to ship width
  static constexpr float kKamikazeAuraRadiusScale        = 0.88f;
  /// Radial bias offset for Kamikaze aura expansion
  static constexpr float kKamikazeAuraRadiusBias         = 0.12f;
  /// Outer warning ring radius multiplier
  static constexpr float kKamikazeAuraOuterScale         = 1.30f;
  /// Middle warning ring radius multiplier
  static constexpr float kKamikazeAuraMidScale           = 0.75f;
  /// Inner warning ring radius multiplier
  static constexpr float kKamikazeAuraInnerScale         = 0.35f;

  /// Alien 14 (Albert Alienstein) relativistic spacetime distortion aura scale
  static constexpr float kAlien14AuraRadiusScale         = 0.70f;
}

// =============================================================================
// 7. Audio: Procedural physical synthesizer constants and voice budgets
// =============================================================================
namespace Audio {
  /// Synthesizer sampling rate in Hertz (CD Quality Stereo 2.1)
  static constexpr int    kSampleRateHz                 = 44100;
  /// Maximum concurrent polyphonic synthesizer voice slots
  static constexpr size_t kMaxAudioVoices               = 32;
  /// Lock-free ring buffer command queue capacity (must be power of two)
  static constexpr size_t kAudioRingBufferCapacity      = 128;
  /// Hyperbolic tangent soft-saturation knee preventing digital clipping
  static constexpr float  kSoftLimitKnee                = 0.70f;
  /// Duration of linear onset fade-in applied to stage fanfares (seconds)
  static constexpr float  kFanfareFadeInSec             = 0.30f;
  /// Duration of smooth release fade-out applied to stage fanfares (seconds)
  static constexpr float  kFanfareFadeOutSec            = 0.50f;

  /// Canonical timing window for stage fanfare final tonic resolution chord (seconds)
  static constexpr float  kFanfareTonicResolutionMinSec = 5.20f;
  static constexpr float  kFanfareTonicResolutionMaxSec = 5.40f;
  /// Canonical duration for stage fanfare final tonic resolution chord (seconds)
  static constexpr float  kFanfareTonicDurationMinSec    = 0.50f;
  static constexpr float  kFanfareTonicDurationMaxSec    = 0.75f;
  /// Start of the real silence window in stage fanfares (seconds)
  static constexpr float  kFanfareSilenceWindowStartSec  = 5.85f;

  /// Synthesis envelope duration for Starfighter primary laser cannon (seconds)
  static constexpr float  kLaserDurationSec             = 0.50f;
  /// Synthesis envelope duration for light alien explosion pop (seconds)
  static constexpr float  kAlienPopLightDurationSec     = 0.55f;
  /// Synthesis envelope duration for medium alien explosion pop (seconds)
  static constexpr float  kAlienPopMediumDurationSec    = 0.60f;
  /// Synthesis envelope duration for heavy alien explosion pop (seconds)
  static constexpr float  kAlienPopHeavyDurationSec     = 0.70f;
  /// Synthesis envelope duration for Kamikaze proximity dive warning siren (seconds)
  static constexpr float  kKamikazeAlertDurationSec     = 0.30f;
  /// Synthesis envelope duration for Kamikaze thermal core breach blast (seconds)
  static constexpr float  kKamikazeExplosionDurationSec = 0.90f;
  /// Synthesis envelope duration for Starfighter hull impact impact (seconds)
  static constexpr float  kPlayerHitDurationSec         = 0.32f;
  /// Synthesis envelope duration for Starfighter total destruction explosion (seconds)
  static constexpr float  kPlayerDestructionDurationSec = 1.60f;
  /// Synthesis envelope duration for Tactical Nuke detonation rumble (seconds)
  static constexpr float  kNukeBlastDurationSec         = 2.20f;
  /// Synthesis envelope duration for Rapid Fire power-up jingle (seconds)
  static constexpr float  kPowerupFireDurationSec       = 0.22f;
  /// Synthesis envelope duration for Multi-Cannon power-up jingle (seconds)
  static constexpr float  kPowerupMultiDurationSec      = 0.28f;
  /// Synthesis envelope duration for Speed Boost power-up jingle (seconds)
  static constexpr float  kPowerupSpeedDurationSec      = 0.22f;
  /// Synthesis envelope duration for Shield Restore fanfare jingle (seconds)
  static constexpr float  kShieldRestoreDurationSec     = 0.62f;
}

// =============================================================================
// 8. Starfield: Cosmic background density, reference bounds, and parallax layers
// =============================================================================
namespace Starfield {
  /// Reference resolution width for square-root surface area star density scaling (pixels)
  static constexpr double   kBaseResolutionWidthPixels   = 1280.0;
  /// Reference resolution height for square-root surface area star density scaling (pixels)
  static constexpr double   kBaseResolutionHeightPixels  = 720.0;
  /// Nominal star count generated at baseline reference resolution
  static constexpr double   kBaseStarCount               = 120.0;
  /// Hard minimum star count ceiling preventing starfield starvation
  static constexpr size_t   kMinStarsCount               = 90;
  /// Hard maximum star count ceiling preventing GPU fillrate saturation
  static constexpr size_t   kMaxStarsCount               = 425;
  /// Parallax threshold splitting distant background points from luminous foreground stars
  static constexpr float    kForegroundLayerThreshold    = 0.82f;

  /// Fixed-point twinkle phase: 16-bit phase, top 8 bits address the lookup table
  static constexpr uint32_t kTwinklePhaseScale           = 65536u;
  /// Size of the trigonometric twinkle brightness lookup table
  static constexpr uint32_t kTwinkleTableSize            = 256u;
  /// Maximum foreground star quads represented by the compile-time index table
  static constexpr size_t   kMaxStarQuadCount            = 2048;

  /// 2π (tau) for the compile-time twinkle LUT and spawn phase (radians)
  static constexpr float    kTau                         = 6.28318530718f;

  /// Minimum scintillation brightness. Stars never fully vanish at the trough.
  static constexpr float    kTwinkleFloor                = 0.38f;
  /// Peak scintillation brightness (LUT sample at the flash crest)
  static constexpr float    kTwinkleCeil                 = 1.00f;
  /// 2nd-harmonic weight: atmospheric seeing ripples (LUT static-init only)
  static constexpr float    kTwinkleHarmonic2            = 0.18f;
  /// 3rd-harmonic weight: sharper stellar flashes (LUT static-init only)
  static constexpr float    kTwinkleHarmonic3            = 0.08f;

  // ---------------------------------------------------------------------------
  // Authoring knobs -- tune THESE three groups. Everything else is derived.
  //   TIME  : kTwinklePeriodMinSec / kTwinklePeriodMaxSec
  //   FALL  : kMidFieldFallSpeedMinPx / MaxPx, kForegroundFallSpeedMinPx / MaxPx
  //   SIZE  : kMidFieldHalfSizeMinPx / MaxPx,  kForegroundSizeMinPx / MaxPx
  // ---------------------------------------------------------------------------

  /// Fastest scintillation cycle (seconds). Explicit TWINKLE TIME control.
  static constexpr float    kTwinklePeriodMinSec         = 1.50f;
  /// Slowest scintillation cycle (seconds). Explicit TWINKLE TIME control.
  static constexpr float    kTwinklePeriodMaxSec         = 3.75f;

  /// Small-star fall speed (pixels per 60 Hz tick). Explicit FALL SPEED control.
  static constexpr float    kMidFieldFallSpeedMinPx      = 0.10f;
  static constexpr float    kMidFieldFallSpeedMaxPx      = 0.15f;
  /// Large-star fall speed (pixels per 60 Hz tick). Explicit FALL SPEED control.
  static constexpr float    kForegroundFallSpeedMinPx    = 0.20f;
  static constexpr float    kForegroundFallSpeedMaxPx    = 0.25f;

  /// Small-star half-size at pulse trough (px, 1x DPI). Explicit SIZE MIN.
  /// Diameter = 2 * this. Never a single pixel.
  static constexpr float    kMidFieldHalfSizeMinPx       = 2.2f;
  /// Small-star half-size at pulse peak (px, 1x DPI). Explicit SIZE MAX.
  static constexpr float    kMidFieldHalfSizeMaxPx       = 3.2f;
  /// Large-star quad size at pulse trough (px, 1x DPI). Explicit SIZE MIN.
  static constexpr float    kForegroundSizeMinPx         = 8.0f;
  /// Large-star quad size at pulse peak (px, 1x DPI). Explicit SIZE MAX.
  static constexpr float    kForegroundSizeMaxPx         = 20.0f;

  /// Derived rad/tick = tau / (period_sec * sim_hz). Do not tune; edit period.
  static constexpr float    kTwinkleSpeedMax =
      kTau / (kTwinklePeriodMinSec * Simulation::kSimulationFrequencyHz);
  static constexpr float    kTwinkleSpeedMin =
      kTau / (kTwinklePeriodMaxSec * Simulation::kSimulationFrequencyHz);

  /// Alpha floor for MidField (keeps distant stars readable at the trough)
  static constexpr float    kMidFieldAlphaFloor          = 0.32f;
  /// Integer alpha floor for Foreground (80/255). LUT floor is the real floor.
  static constexpr uint8_t  kForegroundAlphaFloor        = 80;

  /// DPI clamp for star quads (independent of ship / alien sprite scale)
  static constexpr float    kStarScaleDpiMin             = 0.9f;
  static constexpr float    kStarScaleDpiMax             = 1.6f;

  /// 64x64 flare Gaussian core sigma². Wider than 9 so a 7 px disk still glows.
  static constexpr float    kFlareCoreSigma2             = 22.0f;
  /// Peak gain of the Airy-disk core (pre-clamp)
  static constexpr float    kFlareCoreGain               = 1.70f;
  /// Exponential halo length (px in the 64x64 flare)
  static constexpr float    kFlareHaloSigma              = 12.0f;
  /// Halo gain. Soft corona around both small and large stars.
  static constexpr float    kFlareHaloGain               = 0.42f;


  /// Star density factor for Level 0 (Deep space / stars disabled)
  static constexpr double   kDensityFactorLevel0         = 0.00;
  /// Star density factor for Level 1 (Classic arcade starfield)
  static constexpr double   kDensityFactorLevel1         = 0.60;
  /// Star density factor for Level 2 (Dense cinematic baseline)
  static constexpr double   kDensityFactorLevel2         = 1.50;
  /// Star density factor for Level 3 (Balanced deep-space nebula)
  static constexpr double   kDensityFactorLevel3         = 2.00;
  /// Star density factor for Level 4 (Hyper-dense immersive field)
  static constexpr double   kDensityFactorLevel4         = 3.50;

  /// Pure function mapping user detail levels (0..4) to starfield density factors
  [[nodiscard]] inline constexpr double GetDensityFactor(int level) noexcept {
    switch (level) {
      case 0:  return kDensityFactorLevel0;
      case 1:  return kDensityFactorLevel1;
      case 2:  return kDensityFactorLevel2;
      case 3:  return kDensityFactorLevel3;
      case 4:  return kDensityFactorLevel4;
      default: return kDensityFactorLevel2;
    }
  }

  /// Remap a [0, 1] twinkle sample onto a 0..1 pulse, independent of LUT floor.
  /// Size uses Pulse01 so a 38-100 % brightness swing becomes a full breath;
  /// alpha uses the raw sample so the star dims without disappearing.
  [[nodiscard]] inline constexpr float Pulse01(float twinkle) noexcept {
    const float span = kTwinkleCeil - kTwinkleFloor;
    return std::clamp((twinkle - kTwinkleFloor) / span, 0.0f, 1.0f);
  }
}

// =============================================================================
// 9. Special Entities: Quantum orbital parameters for Alien 4 and Alien 14
// =============================================================================
namespace SpecialEntities {
  // ---------------------------------------------------------------------------
  // 9.1 Alien 4 (Marie Curielien): Quantum Orbital Electrosphere
  // ---------------------------------------------------------------------------
  /// Number of pre-computed trigonometric knots forming the orbital ellipse geometry
  static constexpr size_t kAlien4OrbitKnotSteps           = 36;
  /// Number of quantum particles orbiting inside the electrosphere
  static constexpr int    kAlien4ElectronCount            = 2;
  /// Angular velocity of electron orbit in radians per second
  static constexpr float  kAlien4ElectronAngularSpeed     = 3.0f;
  /// Orbital major axis ellipse scale relative to starship aura
  static constexpr float  kAlien4ElectronRadiusScale      = 0.90f;
  /// Orbital minor axis ellipse scale (inclined perspective projection)
  static constexpr float  kAlien4ElectronMinorRadiusScale = 0.40f;
  /// Physical diameter of the electron core in pixels
  static constexpr float  kAlien4ElectronRadiusPixels     = 2.5f;
  /// Radius of the glowing quantum aura surrounding orbiting electrons in pixels
  static constexpr float  kAlien4ElectronAuraRadiusPixels = 8.0f;
  /// Orbital inclination plane angle in radians (~36 degrees)
  static constexpr float  kAlien4OrbitTiltRad             = 0.628f;
  /// Alien 4 electrosphere animation dynamics (deterministic 60 Hz phase step)
  static constexpr float  kAlien4ElectronPhaseStep        = 0.020f;
  /// Outer aura radius scale relative to ship width
  static constexpr float  kAlien4AuraRadiusScale          = 0.85f;
  /// Midpoint of the breathing aura pulse
  static constexpr float  kAlien4AuraPulseMid             = 0.82f;
  /// Amplitude excursion of the breathing aura pulse
  static constexpr float  kAlien4AuraPulseAmplitude       = 0.18f;
  /// Frequency of the electrosphere breathing pulsation
  static constexpr float  kAlien4AuraPulseFrequency       = 3.5f;
  /// Inner aura scale relative to outer aura
  static constexpr float  kAlien4InnerAuraScale           = 0.40f;
  /// Alpha decay scale factor applied upon first non-fatal hit to Alien 4
  static constexpr float  kAlien4AlphaDecayScale           = 0.50f;
  /// Hit points required to neutralize Alien 4
  static constexpr int    kAlien4InitialHealth             = 2;
  /// Speed multiplier for electron orbit in alpha-decayed excited state
  static constexpr float  kAlien4DecayedOrbitSpeedMult     = 2.0f;
  /// Flight velocity multiplier applied to Alien 4 in alpha-decayed excited state (+30% speed)
  static constexpr float  kAlien4DecayedSpeedMult          = 1.30f;
  /// Probability of Alien 4 spawning with an active electrosphere
  static constexpr float  kAlien4ElectrosphereProbability  = 0.20f;

  // ---------------------------------------------------------------------------
  // 9.2 Alien 14 (Albert Alienstein): Relativistic Spacetime Warp Evasion Mechanics
  // ---------------------------------------------------------------------------
  /// Probability p (0.0 to 1.0) that an Alien 14 starship possesses spacetime warp evasion capability
  static constexpr float  kAlien14WarpEvasionProbability  = 0.60f;
  /// Number of spacetime warp activations n allowed per starship
  static constexpr int    kAlien14NumWarpEvasions          = 2;
  /// Trigger distance multiplier y: evasion activates when player bullet distance < y * radius
  /// where radius is defined as distance from starship center to greatest extremity (half-diagonal)
  static constexpr float  kAlien14WarpTriggerRadiusFactor  = 2.4f;
  /// High warp transit velocity in pixels per 60 Hz frame (at 1.0x baseline scale)
  static constexpr float  kAlien14WarpSpeedPixels          = 32.0f;
  /// Capacity of the fading relativistic afterimage ghost trail buffer
  static constexpr size_t kAlien14WarpGhostCapacity        = 8;
  /// Linear decay rate per frame for ghost afterimages (fades out in ~12 frames)
  static constexpr float  kAlien14WarpGhostFadeSpeed       = 0.085f;
  /// Cooldown interval x in seconds between consecutive spacetime warp evasions (anti-frenetic pacing)
  static constexpr float  kAlien14WarpCooldownSeconds      = 1.4f;
  /// Hard cap on warp travel frames (~1.0 s @ 60 Hz). Guarantees is_warping_ clears.
  static constexpr int    kAlien14WarpMaxFrames           = 60;
  /// Minimum honeycomb-cell separation (Chebyshev max(|Δcol|,|Δrow|)) between origin and warp destination.
  /// Exclusion zone is a square of side 2N-1; enables O(1) restricted random draw without rejection loops.
  static constexpr int    kAlien14WarpMinCellSeparation    = 4;
}

// =============================================================================
// 10. Telemetry: Hardware performance OSD monitor metrics
// =============================================================================
namespace TelemetryConfig {
  /// Polling rate interval for asynchronous hardware metric sampling (500 ms = 0.5 s)
  static constexpr uint64_t kSampleIntervalNs = 500'000'000ULL;
  /// Typographic font point size for the telemetry OSD banner
  static constexpr float    kFontSize         = 21.6f;
  /// FPS color threshold below which the meter is displayed in red
  static constexpr double   kFpsWarnThreshold = 40.0;
  /// FPS color threshold above which the meter is displayed in green, amber below
  static constexpr double   kFpsGoodThreshold = 58.0;
}

} // namespace GameRules

#endif  // CONSTANTS_H
