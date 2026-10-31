#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

/// Circle constant (SSOT): every degree<->radian conversion in the codebase
/// spells pi through this single definition.
inline constexpr float Pi = std::numbers::pi_v<float>;

// GameRules: canonical SSOT for gameplay tuning. Pure C++20 header, deep DRY
// (derived constants, zero alias wrappers), zero allocation on hot loops.
namespace GameRules {

// =============================================================================
// 1. Simulation: rigid 60 Hz deterministic clock -- identical gameplay on any monitor
namespace Simulation {
  /// Logic simulation frequency in Hertz (rigid clock decoupled from monitor refresh rates)
  static constexpr float    kSimulationFrequencyHz     = 60.0f;
  /// Standard fixed physics delta time in seconds (1/60 s)
  static constexpr double   kFixedStepDurationSec      = 1.0 / static_cast<double>(kSimulationFrequencyHz);
  /// Fixed step duration in integer nanoseconds (16.666 ms)
  static constexpr uint64_t kFixedStepNanoseconds      = 16'666'667ULL;
  /// Maximum delta time clamp (250 ms): prevents catch-up death spirals
  /// during OS compositor stalls
  static constexpr uint64_t kMaxFrameDeltaNanoseconds  = 250'000'000ULL;
  /// Maximum accumulator debt ceiling bounding physics backlog to at most 2 steps (33.3 ms)
  static constexpr uint64_t kMaxAccumulatorNanoseconds = kFixedStepNanoseconds * 2ULL;
  /// Jitter slack (0.25 ms) granted to the FIRST step of each frame so a 60 Hz VBlank that
  /// arrives a hair early does not flip the loop between 0-step and 2-step frames
  static constexpr uint64_t kJitterSlackNanoseconds    = 250'000ULL;
  /// Spiral-of-death guard: hard cap of catch-up steps executed per rendered frame
  static constexpr int      kMaxStepsPerFrame          = 2;
  /// Frame gap (3 missed VBlanks, 50 ms) classed as an EXTERNAL stall; it is
  /// charged as ONE nominal step instead of a catch-up burst
  static constexpr uint64_t kHitchDeltaNanoseconds     = kFixedStepNanoseconds * 3ULL;
  /// Master PRNG seed ensuring deterministic simulation, test reproducibility, and replays
  static constexpr uint32_t kDefaultSimulationSeed     = 0x1337BEEFu;
}

// =============================================================================
// 2. Stage Progression: campaign structure, fanfare display, and warp travel
namespace Progression {
  /// Total combat waves composing a single stage cycle before difficulty re-tiering
  static constexpr int   kWavesPerStage                 = 15;
  /// Seconds of the stage-cleared fanfare and hyperspace star transition
  static constexpr float kStageFanfareDurationSeconds   = 6.0f;
  /// Default scored length (seconds) of a soundtrack excerpt; a track's own
  /// ScoreManifest may publish a longer full-score length that clamps windows.
  static constexpr float kSoundtrackExcerptFullSeconds  = 30.0f;
  /// Frames before the incoming armada begins its incursion (1.0 s @ 60 Hz)
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
// 3. Player: interceptor kinetics, weapon fire intervals, and shield health
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
    return std::max(kMinFireIntervalFrames, static_cast<int>((kBaseFireIntervalFrames / rate_mult) + 0.5f));
  }
}

// =============================================================================
// 4. Fleet: armada formation geometry, movement speeds, and attack pacing
namespace Fleet {
  /// Base unscaled visual width of alien starships in 1080p design coordinates (scale 1.0)
  static constexpr float kAlienBaseWidthPixels          = 84.0f;
  /// Base unscaled visual height of alien starships in 1080p design coordinates (scale 1.0)
  static constexpr float kAlienBaseHeightPixels         = 84.0f;
  /// Formation grid layout horizontal spacing between alien centers
  static constexpr float kAlienHorizontalSpacingPixels  = 93.0f;
  /// Formation grid layout vertical spacing between alien centers
  static constexpr float kAlienVerticalSpacingPixels    = 95.0f;
  /// Vertical cruise horizon line for the armada formation
  static constexpr float kFleetBaseCruiseYPixels        = 82.0f;
  /// Velocity floor (px/frame) keeping fleet interpolation from stalling
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
  /// Probability p that an alien bomb deploys with homing thrusters (Seeker)
  static constexpr float kSeekerMissileProbability      = 0.30f;
  /// Maximum trajectory deflection angle for vector-guided missiles (degrees).
  static constexpr float kMaxDeflectionAngleDeg         = 20.0f;
  /// Minimum guaranteed evasion corridor preventing converging cross-fire scissor traps (pixels)
  static constexpr int   kSafeEvasionCorridorPixels     = 118;
  /// Proximity breach radius multiplier relative to alien width triggering kamikaze detonation
  static constexpr float kKamikazeBlastRadiusMultiplier = 1.4f;
  /// Total frames composing an aerobatic alien spin animation
  static constexpr int   kAlienSpinAnimationFrames      = 46;
  /// Hard hull turn-rate limit in degrees per 60 Hz frame: every attitude
  /// change (dive, docking arc, drift) is a rate-limited sweep, never a snap
  static constexpr float kMaxTurnRateDeg                = 5.5f;
  /// Ease-out share of the remaining turn arc consumed per frame (angular
  /// partitioning: wide arcs sweep fast, the tail lands without stair-steps)
  static constexpr float kTurnResponseFactor            = 0.18f;
  /// Turn convergence floor in degrees per frame: at or below this remaining
  /// arc the hull lands exactly on the target bearing (no dead-zone freeze)
  static constexpr float kTurnSnapThresholdDeg          = 0.5f;
  /// Lateral armada drift in design pixels per frame at scale 1.0; the
  /// formation reverses direction at the min/max cruise bounds
  static constexpr float kFleetCruiseDriftPixels        = 1.0f;
  /// Heading rotation cap (degrees/frame) of the re-docking arc: station
  /// returns fly a banked curve nose-first instead of a straight side-slide
  static constexpr float kJoinHeadingTurnRateDeg        = 3.5f;
  /// Docking capture radius in frames-of-travel: inside it the alien homes
  /// straight onto its slot, so the rate-limited arc can never orbit forever
  static constexpr float kJoinDirectDockVelocityFactor  = 4.0f;
  /// Inertial share of the previous lateral offset kept by each attack-dive
  /// control point: bearings change gradually so the hull tracks nose-first
  static constexpr float kAttackDiveLateralInertia      = 0.55f;
  /// Random lateral jitter added per dive control point (normalized 1024 units)
  static constexpr int   kAttackDiveLateralJitterUnits  = 110;
  /// Hard cap of the accumulated lateral swing per dive step (normalized units)
  static constexpr float kAttackDiveLateralSwingCapUnits = 150.0f;
  /// Minimum downward advance per dive control point (normalized units):
  /// keeps the raw spline under 40 points for any on/off-screen launch
  static constexpr float kAttackDiveMinAdvanceUnits     = 1024.0f / 26.0f;
  /// Random extra downward advance per dive control point (normalized units)
  static constexpr int   kAttackDiveAdvanceJitterUnits  = 159;
  /// Normalized Y where dive spline generation stops (just below the screen)
  static constexpr float kAttackDiveExitNormalizedY     = 1050.0f;

  /// Compile-time Honeycomb geometry tuning
  static constexpr float kFormationRowStagger          = 0.50f;
  static constexpr int   kFormationStage2CurveCenter   = 4;
  static constexpr int   kFormationStage2CurvePlateau  = 3;
  static constexpr float kFormationStage3WaveDip       = 0.25f;
  static constexpr float kFormationStage3FlankDivisor  = 6.0f;
  static constexpr float kFormationStage4WedgeHeight   = 1.25f;
  static constexpr float kFormationStage4WedgeDivisor  = 5.0f;
  static constexpr float kFormationStage5WaveDip       = 0.50f;
  static constexpr float kFormationStage5FlankDivisor  = 4.0f;

  /// Stage 1 minimum fleet trajectory velocity in pixels per frame
  static constexpr float kStage1MinSpeed                = 5.0f;
  /// Stage 1 maximum fleet trajectory velocity in pixels per frame
  static constexpr float kStage1MaxSpeed                = 7.5f;
  /// Stage 2 minimum fleet trajectory velocity in pixels per frame
  static constexpr float kStage2MinSpeed                = 8.0f;
  /// Stage 2 maximum fleet trajectory velocity in pixels per frame
  static constexpr float kStage2MaxSpeed                = 10.5f;
  /// Stage 3 minimum fleet trajectory velocity in pixels per frame
  static constexpr float kStage3MinSpeed                = 11.0f;
  /// Stage 3 maximum fleet trajectory velocity in pixels per frame
  static constexpr float kStage3MaxSpeed                = 13.5f;
  /// Stage 4 minimum fleet trajectory velocity in pixels per frame
  static constexpr float kStage4MinSpeed                = 14.0f;
  /// Stage 4 maximum fleet trajectory velocity in pixels per frame
  static constexpr float kStage4MaxSpeed                = 16.5f;
  /// Stage 5+ minimum fleet trajectory velocity in pixels per frame
  static constexpr float kStage5MinSpeed                = 17.0f;
  /// Stage 5+ maximum fleet trajectory velocity in pixels per frame
  static constexpr float kStage5MaxSpeed                = 20.0f;

  /// Attack dive delay ceiling in frames for Stage 1 (5 seconds @ 60 Hz)
  static constexpr int   kStage1MaxAttackWaitFrames     = 300;
  /// Attack dive delay ceiling in frames for Stage 2 (4 seconds @ 60 Hz)
  static constexpr int   kStage2MaxAttackWaitFrames     = 240;
  /// Attack dive delay ceiling in frames for Stage 3 (3 seconds @ 60 Hz)
  static constexpr int   kStage3MaxAttackWaitFrames     = 180;
  /// Attack dive delay ceiling in frames for Stage 4 (2 seconds @ 60 Hz)
  static constexpr int   kStage4MaxAttackWaitFrames     = 120;
  /// Attack dive delay ceiling in frames for Stage 5+ (1 second @ 60 Hz)
  static constexpr int   kStage5MaxAttackWaitFrames     = 60;
  /// Attack dive delay ceiling in frames for independent wandering aliens (3 seconds @ 60 Hz)
  static constexpr int   kWandererMaxWaitFrames         = 180;

  /// Hard cap of live armada starships: one canonical spelling for every
  /// spawn-site ceiling, pool reserve and selection scratch array (SSOT).
  static constexpr size_t kMaxAliensCount               = 256;
  /// Minimum frames an alien waits before commencing an attack dive
  static constexpr int   kMinAttackWaitFrames           = 30;
  /// Minimum frames between consecutive convoy spawn ranks
  static constexpr int   kMinSpawnWaitFrames            = 5;
  /// Convoy rank separation as a factor of the formation height
  static constexpr float kConvoySpawnGapFactor          = 1.10f;
  /// Rank separation factor when the next convoy shares the arrival lane
  static constexpr float kConvoyGapSameLaneFactor       = 1.75f;
  /// Rank separation factor when the next convoy uses a different lane
  static constexpr float kConvoyGapOtherLaneFactor      = 1.18f;
  /// Off-screen margin factor keeping arrivals fully hidden before entry
  static constexpr float kArrivalOffscreenMarginFactor  = 1.15f;
  /// Denominator of the 1/N probability that a diver performs a barrel spin
  static constexpr int   kAlienSpinChanceDenominator    = 140;
  /// Kamikaze dive spline: midpoint placement ratio along start -> player
  static constexpr float kKamikazeDiveMidpointRatio     = 0.45f;
  /// Kamikaze dive spline: lateral jitter of the midpoint (normalized units)
  static constexpr int   kKamikazeDiveLateralJitterUnits = 50;
  /// Kamikaze dive spline: overshoot past the player (normalized units)
  static constexpr float kKamikazeDiveOvershootUnits    = 140.0f;

  /// Computes current armada trajectory velocity interpolated across wave progression
  [[nodiscard]] inline constexpr float ComputeSpeed(int level, int max_levels) noexcept {
    const int   stage         = Progression::WaveToStage(level);
    const int   wave_in_stage = Progression::WaveInStage(level);
    const float intra         = static_cast<float>(wave_in_stage) / static_cast<float>(std::max(1, max_levels));
    if (stage == 1) return kStage1MinSpeed + intra * (kStage1MaxSpeed - kStage1MinSpeed);
    else if (stage == 2) return kStage2MinSpeed + intra * (kStage2MaxSpeed - kStage2MinSpeed);
    else if (stage == 3) return kStage3MinSpeed + intra * (kStage3MaxSpeed - kStage3MinSpeed);
    else if (stage == 4) return kStage4MinSpeed + intra * (kStage4MaxSpeed - kStage4MinSpeed);
    else return kStage5MinSpeed + intra * (kStage5MaxSpeed - kStage5MinSpeed);
  }

  /// Returns maximum wait frames before a standard alien commences an attack dive
  /// for a given stage cycle (Deep DRY core shared by wave-based and trajectory callers)
  [[nodiscard]] inline constexpr int GetMaxAttackWaitFramesForStage(int stage) noexcept {
    if (stage <= 1) return kStage1MaxAttackWaitFrames;
    if (stage == 2) return kStage2MaxAttackWaitFrames;
    if (stage == 3) return kStage3MaxAttackWaitFrames;
    if (stage == 4) return kStage4MaxAttackWaitFrames;
    return kStage5MaxAttackWaitFrames;
  }

  /// Returns maximum wait frames before a standard alien commences an attack dive
  [[nodiscard]] inline constexpr int GetMaxAttackWaitFrames(int level) noexcept {
    return GetMaxAttackWaitFramesForStage(Progression::WaveToStage(level));
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
// 5. Combat: object pools, scoring values, and supply drop probabilities
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
  /// Total point-batch capacity reserved by the explosion renderer, split
  /// evenly across its four color bands (bright/orange/red/dark)
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
  /// Share of the source body's velocity inherited by explosion debris
  static constexpr float   kExplosionResidualVelocityFactor = 0.15f;
  /// Slack (px) widening an alien's Y-band so fast projectiles travelling
  /// between two 60 Hz ticks can never tunnel through the early-rejection filter
  static constexpr int     kCollisionBandMarginPixels       = 40;

  /// Score points awarded for destroying a standard armada alien
  static constexpr uint64_t kScorePerAlienHit               = 10ULL;
  /// Score points awarded for neutralizing an aggressive Kamikaze diver (+50 pts)
  static constexpr uint64_t kScorePerKamikazeAlienHit       = 50ULL;
  /// Score points awarded per enemy starship vaporized by a Tactical Nuke
  static constexpr uint64_t kScorePerAlienNuked             = 20ULL;
  /// Milestone score progression required to earn an additional shield (Shield-UP)
  static constexpr uint64_t kExtraShieldScoreStep           = 1000ULL;

  /// Maximum distinct supply pod drops per 15-wave stage (each type once)
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
  /// Screen-edge margin (px) of the anti-scissor trap check: a predicted
  /// intercept beyond this margin counts as a wall-ward (trapping) trajectory
  static constexpr int     kTrapCheckScreenEdgeMarginPixels = 40;

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

  /// One RGB triplet: the canonical spelling of every engine exhaust color.
  struct RgbColor { std::uint8_t r, g, b; };
  /// Seeker exhaust palette indexed by engine_type (SSOT): the defaults of
  /// BulletsManager::Add and the alien fire path both read slot 0.
  inline constexpr std::array<RgbColor, kEngineTypeCount> kSeekerEngineColors{{
      {210, 60, 255}, {255, 180, 25}, {0, 230, 255}, {50, 255, 100}}};
  /// Alien 14 relativistic spacetime-continuum propulsion tint
  inline constexpr RgbColor kAlien14EngineColor{0, 220, 255};
  /// Alien 4 radioactive seeker emission tint
  inline constexpr RgbColor kAlien4RadioEngineColor{50, 255, 100};

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
// 6. Visuals: floating HUD typography sizes and procedural visual FX
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
  /// Seconds the mouse pointer remains visible over the game screen after
  /// the last mouse movement; once this idle window elapses the cursor hides
  static constexpr float kCursorIdleHideSeconds          = 5.0f;

  /// Per-Present() slice (us) of the budgeted font prewarm after a scale change
  /// (~2.5% of a 60 Hz frame): spreads rasterization across many frames.
  static constexpr int   kFontPrewarmBudgetMicroseconds  = 1500;

  /// Row cap of one atlas publish slice: keeps bulk initialization under the
  /// frame budget while the full page still converges within the prewarm window.
  static constexpr int   kFontBandFlushRowBudget         = 64;

  /// Rows of [range_min_y, range_max_y) to upload this frame; a non-positive
  /// budget means unlimited, else the exact advance converging the range.
  [[nodiscard]] constexpr int BandFlushRows(int range_min_y, int range_max_y,
                                            int row_budget) noexcept {
    const int pending = range_max_y - range_min_y;
    if (pending <= 0) return 0;
    if (row_budget <= 0 || pending <= row_budget) return pending;
    return row_budget;
  }

  /// Rows left for the zero-publish channel after the glyph band spent its share
  /// of ONE per-frame row budget; 0 means skip the zero slice for this frame.
  [[nodiscard]] constexpr int ZeroPublishRowBudget(int band_rows_uploaded,
                                                   int row_budget) noexcept {
    const int left = row_budget - band_rows_uploaded;
    return (left > 0) ? left : 0;
  }

  /// Stop policy of the budgeted prewarm: true once claimed band rows reach one
  /// publish slice, capping the coalesced upload; an empty band never exhausts.
  [[nodiscard]] constexpr bool BandClaimRowsExhausted(int band_min_y,
                                                      int band_max_y,
                                                      int row_budget) noexcept {
    return (band_max_y - band_min_y) >= row_budget;
  }

  /// HUD plate textures allocate in BUCKETED sizes so an interactive resize only
  /// recreates GPU memory at bucket crossings -- never per drag pixel.
  static constexpr int   kHudPlateWidthBucketPx          = 256;
  static constexpr int   kHudPlateHeightBucketPx         = 64;
  /// Waits for stable geometry before a geometry-only plate rebake. Content
  /// updates bypass this debounce; both paths share the settling gate.
  static constexpr int   kHudPlateSettleFrames           = 3;
  /// Cap (frames) of the HUD structural storm deferral: bounds stale-plate time
  /// while rapid preset/fullscreen bursts still collapse into ONE rebake.
  static constexpr int   kHudPlateStormMaxDeferFrames    = 6;
  /// Cap (frames) of the settling deferral for TEXT-redrawing plate rebakes
  /// (HUD score AND telemetry): cold glyphs cost tens of ms, so bakes wait.
  static constexpr int   kPlateSettlingMaxDeferFrames    = 30;
  /// Bounds geometry-only menu rebakes during resize storms. Menu and HUD
  /// use separate budgets because their rebake costs differ.
  static constexpr int   kMenuPageSettleFrames           = 3;
  static constexpr int   kMenuPageStormMaxDeferFrames    = 15;

  /// Round-up bucketing of plate dimensions (pure integer math, degenerates
  /// clamp to 0); the bucket never falls below `value`, so blits stay inside.
  [[nodiscard]] constexpr int HudPlateBucket(int value, int step) noexcept {
    if (value <= 0 || step <= 0) return 0;
    return ((value + step - 1) / step) * step;
  }

  /// Tracks frames spent deferring the current structural change. Resetting
  /// on settle or clean state gives every resize storm a fresh budget.
  [[nodiscard]] constexpr int NextStormDeferFrames(int current_defer_frames,
                                                  bool dims_change_pending,
                                                  bool geometry_settled) noexcept {
    return (dims_change_pending && !geometry_settled) ? current_defer_frames + 1
                                                      : 0;
  }

  /// Structural-rebake storm policy: apply NOW when no texture exists yet or
  /// geometry settled; otherwise defer up to max_defer_frames, then force.
  [[nodiscard]] constexpr bool StormRebakeApplyNow(bool texture_exists,
                                                 bool geometry_settled,
                                                 int defer_frames,
                                                 int max_defer_frames) noexcept {
    if (!texture_exists) return true;
    if (geometry_settled) return true;
    if (max_defer_frames <= 0) return true;
    return defer_frames >= max_defer_frames;
  }

  /// Defer clock of the settling gate: counts the frames a wanted text redraw
  /// waits while the font engine settles; any clean state resets it to zero.
  [[nodiscard]] constexpr int NextSettlingDeferFrames(int current_defer_frames,
                                                      bool redraw_wanted,
                                                      bool text_settling) noexcept {
    return (redraw_wanted && text_settling) ? current_defer_frames + 1 : 0;
  }

  /// Settling gate: proceed at once when NOT settling; else defer up to
  /// max_defer_frames (a non-positive cap disables the deferral).
  [[nodiscard]] constexpr bool SettlingRebakeMayProceed(bool text_settling,
                                                        int defer_frames,
                                                        int max_defer_frames) noexcept {
    if (!text_settling) return true;
    if (max_defer_frames <= 0) return true;
    return defer_frames >= max_defer_frames;
  }

  /// Calm-present factor of the rebake gate: the last SDL_RenderPresent counts
  /// as calm if it blocked <= factor x nominal frame ms (1 missed VBlank is ok).
  static constexpr float kCalmRebakePresentFactor   = 1.75f;
  /// Cap (frames) of the calm deferral (HUD score AND telemetry): bounded
  /// staleness against a pathological present storm (~0.5 s at 60 Hz).
  static constexpr int   kCalmRebakeMaxDeferFrames  = 30;

  [[nodiscard]] constexpr bool PresentIsCalmForRebake(
      float last_present_ms, float nominal_frame_ms) noexcept {
    return last_present_ms <= nominal_frame_ms * kCalmRebakePresentFactor;
  }

  /// Defer clock of the calm gate: counts frames a wanted rebake waits on busy
  /// presents; resets to zero the moment it is free to run.
  [[nodiscard]] constexpr int NextCalmDeferFrames(int current_defer_frames,
                                                  bool rebake_wanted,
                                                  bool present_calm) noexcept {
    return (rebake_wanted && !present_calm) ? current_defer_frames + 1 : 0;
  }

  /// Calm gate: proceed on a calm present; while busy defer up to
  /// max_defer_frames (a non-positive cap disables the deferral).
  [[nodiscard]] constexpr bool CalmRebakeMayProceed(bool present_calm,
                                                    int defer_frames,
                                                    int max_defer_frames) noexcept {
    if (present_calm) return true;
    if (max_defer_frames <= 0) return true;
    return defer_frames >= max_defer_frames;
  }

  /// Wall-clock budget (us) of ONE plate warm slice: pre-warms plate strings in
  /// cheap slices so the bake frame pays blits only, never cold rasterization.
  static constexpr int   kPlateWarmBudgetMicroseconds = 3000;
  /// Cap (frames) of the HUD plate warm deferral: on expiry the bake proceeds
  /// cold (correctness before cadence); the stale plate blits throughout.
  static constexpr int   kPlateWarmMaxDeferFrames     = 12;

  /// Defer clock of the warm gate: counts frames a wanted rebake waits on an
  /// incomplete warm pass; resets to zero once the pass completes.
  [[nodiscard]] constexpr int NextPlateWarmDeferFrames(int current_defer_frames,
                                                       bool warm_incomplete) noexcept {
    return warm_incomplete ? current_defer_frames + 1 : 0;
  }

  /// Warm gate: a rebake runs immediately on warm glyphs; a COLD bake is forced
  /// only after kPlateWarmMaxDeferFrames (a non-positive cap disables it).
  [[nodiscard]] constexpr bool PlateWarmBakeMayProceed(int defer_frames) noexcept {
    if (kPlateWarmMaxDeferFrames <= 0) return true;
    return defer_frames >= kPlateWarmMaxDeferFrames;
  }

  /// Grow-only plate capacity: reallocate only when a bucket EXCEEDS the current
  /// texture; shrinks reuse it (the clipped blit selects the visible strip).
  [[nodiscard]] constexpr bool PlateNeedsRegrow(bool texture_exists,
                                                int texture_w, int texture_h,
                                                int needed_w, int needed_h) noexcept {
    if (!texture_exists) return true;
    return needed_w > texture_w || needed_h > texture_h;
  }

  /// Blit gate: a baked plate is reusable ONLY while window width AND OSD scale
  /// still match the bake; stale anchors fall back to direct draws, never blit.
  [[nodiscard]] constexpr bool PlateAnchorsFresh(bool texture_exists,
                                                 int baked_win_w, int win_w,
                                                 float baked_osd_scale,
                                                 float osd_scale) noexcept {
    return texture_exists && baked_win_w == win_w &&
           baked_osd_scale == osd_scale;
  }

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
// 7. Audio: procedural physical synthesizer constants and voice budgets
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

  /// Synthesis envelope duration for Starfighter primary laser cannon (seconds)
  static constexpr float  kLaserDurationSec             = 0.50f;
  /// Synthesis envelope duration for light alien explosion pop (seconds)
  static constexpr float  kAlienPopLightDurationSec     = 0.55f;
  /// Duration of the medium alien's cinematic pressure blast and low-frequency tail (seconds)
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
// 8. Starfield: cosmic background density, reference bounds, and parallax layers
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
  static constexpr float    kTau                         = 2.0f * Pi;

  /// Minimum scintillation brightness. Stars never fully vanish at the trough.
  static constexpr float    kTwinkleFloor                = 0.38f;
  /// Peak scintillation brightness (LUT sample at the flash crest)
  static constexpr float    kTwinkleCeil                 = 1.00f;
  /// 2nd-harmonic weight: atmospheric seeing ripples (LUT static-init only)
  static constexpr float    kTwinkleHarmonic2            = 0.18f;
  /// 3rd-harmonic weight: sharper stellar flashes (LUT static-init only)
  static constexpr float    kTwinkleHarmonic3            = 0.08f;

  // Authoring knobs -- tune THESE three groups only (TIME: twinkle periods;
  // FALL: fall speeds; SIZE: star sizes). Everything else is derived.

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

  /// Remap a [0,1] twinkle sample onto a 0..1 pulse, independent of LUT floor:
  /// size breathes fully; alpha keeps the raw sample so stars never vanish.
  [[nodiscard]] inline constexpr float Pulse01(float twinkle) noexcept {
    const float span = kTwinkleCeil - kTwinkleFloor;
    return std::clamp((twinkle - kTwinkleFloor) / span, 0.0f, 1.0f);
  }
}

// =============================================================================
// 9. Special Entities: quantum orbital parameters for Alien 4 and Alien 14
namespace SpecialEntities {
  // ---- 9.1 Alien 4 (Marie Curielien): quantum orbital electrosphere ----------
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
  static constexpr float  kAlien4DecayedOrbitSpeedMult     = 3.0f;
  /// Flight velocity multiplier applied to Alien 4 in alpha-decayed excited state (+30% speed)
  static constexpr float  kAlien4DecayedSpeedMult          = 1.30f;
  /// Probability of Alien 4 spawning with an active electrosphere
  static constexpr float  kAlien4ElectrosphereProbability  = 0.20f;

  // ---- 9.2 Alien 14 (Albert Alienstein): relativistic warp evasion -----------
  /// Probability p that an Alien 14 starship is born relativistic (warp-capable)
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
  /// Visual recharge interval (s) started the instant a jump is consumed:
  /// a fading hull halo. Purely cosmetic -- it never gates the next trigger.
  static constexpr float  kAlien14WarpCooldownSeconds      = 1.4f;
  /// Hard cap on warp travel frames (~1.0 s @ 60 Hz). Guarantees is_warping_ clears.
  static constexpr int    kAlien14WarpMaxFrames           = 60;
  /// Minimum honeycomb-cell separation N (integer Chebyshev max(|dcol|,|drow|),
  /// no sqrt): landing is forbidden within N cells of the alien AND the player.
  static constexpr int    kAlien14WarpMinCellSeparation    = 5;
  /// Attack-wait floor (frames) re-armed when a relativistic warp completes,
  /// so a relocated Alien 14 re-docks and cruises before it may dive again
  static constexpr int    kAlien14PostWarpAttackWaitMinFrames = 180;
  /// Sole projectile-kill gate: a relativistic Alien 14 dies only when NOT on a
  /// warp trajectory AND zero evasions remain; any evasion left forces a jump.
  [[nodiscard]] constexpr bool WarpEvasionBlocksProjectileKill(
      int warp_evasions_remaining) noexcept {
    return warp_evasions_remaining > 0;
  }
}

// =============================================================================
// Display: design-space to window-space adaptation (engine-wide SSOT)
namespace Display {
  /// Design-space height every tuning constant is authored in: Scale() == 1.0
  /// exactly at this window height (the 1280x720 startup runs at 2/3 scale).
  static constexpr float kDesignBaselineHeightPixels = 1080.0f;
  /// Hard floor keeping sprites, formation spacing and text legible on tiny
  /// windows: below half the baseline height the design space stops shrinking.
  static constexpr float kMinScale                   = 0.5f;
  /// The ONE spelling of the engine-wide scale factor (deep DRY): Gfx recomputes
  /// it per window-metric change; no call site re-derives the formula by hand.
  [[nodiscard]] constexpr float ComputeScale(int window_height_pixels) noexcept {
    return std::max(kMinScale,
                    static_cast<float>(window_height_pixels) /
                        kDesignBaselineHeightPixels);
  }
}

// =============================================================================
// 10. Telemetry: hardware performance OSD monitor metrics
namespace TelemetryConfig {
  /// Sampling window for the FPS/frame-time readout (1 s).
  static constexpr uint64_t kSampleIntervalNs = 1'000'000'000ULL;
  /// Typographic font point size for the telemetry OSD banner
  static constexpr float    kFontSize         = 23.0f;
  /// OSD readability band: banner size = kFontSize x window scale CLAMPED here
  /// (readable at 720p, compact at 4K). SSOT for the draw AND the prewarm plan.
  static constexpr float    kOsdScaleMin      = 0.95f;
  static constexpr float    kOsdScaleMax      = 1.20f;
  /// FPS color threshold below which the meter is displayed in red
  static constexpr double   kFpsWarnThreshold = 40.0;
  /// FPS color threshold above which the meter is displayed in green, amber below
  static constexpr double   kFpsGoodThreshold = 58.0;
}

// =============================================================================
// 11. Diagnostics: frame-hitch flight recorder (opt-in, off the hot path)
namespace Diagnostics {
  /// Environment switch that arms diagnostics ("1" = on). Unset means no diagnostic worker threads.
  static constexpr const char* kHitchLogEnvVar         = "ALIENS_HITCH_LOG";
  /// CLI switch equivalent to ALIENS_HITCH_LOG=1; matching is dash-agnostic
  /// (CliFlags normalizes), so -log/--log/---log all arm this single source.
  static constexpr const char* kHitchLogCliFlag        = "--log";
  /// Optional report threshold override in milliseconds.
  static constexpr const char* kHitchThresholdEnvVar   = "ALIENS_HITCH_MS";
  /// Default report threshold: two fixed 60 Hz steps (~33.3 ms) or longer.
  static constexpr uint64_t    kHitchReportNanoseconds = Simulation::kFixedStepNanoseconds * 2ULL;
  /// Accepted range for ALIENS_HITCH_MS. Invalid values are ignored.
  static constexpr uint64_t    kHitchThresholdMinMs    = 4;
  static constexpr uint64_t    kHitchThresholdMaxMs    = 2000;
  /// Instrumented scope names of the EXTERNAL-WAIT boundaries: driver/window
  /// blocking calls whose wall time is machine-owned, not game arithmetic.
  static constexpr const char* kPresentWaitScopeName   = "SDL_RenderPresent";
  static constexpr const char* kWindowResizeScopeName  = "SdlWindow::Resize";
  static constexpr const char* kWindowFullscreenScopeName =
      "SdlWindow::ToggleFullscreen";
  /// Offscreen-target binding boundary: the driver's implicit sync at a
  /// render-target switch (heaviest right after a display reconfiguration).
  static constexpr const char* kRenderTargetSwitchScopeName =
      "SDL_SetRenderTarget";
  /// Event-pump boundary: SDL_PollEvent wall time is OS/driver round-trips
  /// (one poll can absorb tens of ms in a reconfiguration burst: machine time).
  static constexpr const char* kEventPollScopeName         = "SDL_PollEvent";
  /// The external-wait boundary set (classifier iteration order).
  static constexpr const char* kExternalWaitScopes[]   = {
      kPresentWaitScopeName, kWindowResizeScopeName,
      kWindowFullscreenScopeName, kRenderTargetSwitchScopeName,
      kEventPollScopeName};
  static constexpr std::size_t kExternalWaitScopeCount = 5;
  static constexpr std::size_t kHitchRingCapacity      = 64;
  /// Number of per-frame function aggregates copied into each hitch record.
  static constexpr std::size_t kTopAttributionCount    = 3;
  /// Fixed print width of the reporter's function-name column, sized to the
  /// widest scope name emitted (38 chars); printf precision truncates longer.
  static constexpr int kReportNameColumnChars          = 38;
  /// Minimum per-frame share required before a top attribution line is printed.
  static constexpr std::uint32_t kTopAttributionMinPercent = 5;
  /// Per-frame share at or above which a top rank's self time prints in the
  /// hot color: the visual "this one function owns the frame" signal.
  static constexpr std::uint32_t kTopAttributionHotPercent = 30;
  /// A frame open this long is reported as an in-progress freeze even if
  /// EndFrame() never returns
  static constexpr uint64_t kHitchFreezeWatchdogNanoseconds = 250'000'000ULL;
  /// Watchdog polling interval. The worker sleeps between checks; it never busy-waits.
  static constexpr std::uint32_t kHitchWatchdogPollMilliseconds = 20;
  /// Maximum distinct function offenders retained in the session summary.
  static constexpr std::size_t kSessionOffenderCount = 8;
  /// External-burst annotation: a run of EXTERNAL-verdict reports within
  /// kExternalBurstMaxFrameGap earns ONE note blaming compositor business.
  static constexpr std::uint64_t kExternalBurstMinFrames   = 3;
  static constexpr std::uint64_t kExternalBurstMaxFrameGap = 8;
  /// CPU-starvation discriminator: an ENGINE-dominated slow frame whose thread
  /// got LESS than this share of wall time was off-CPU -> verdict CPU-STARVED.
  static constexpr std::uint32_t kCpuStarvedSharePercent   = 50;
  /// Cadence watch window (frames) of recent totals: 16 ~= 267 ms at 60 Hz --
  /// long enough to measure a pacing swing, short enough to react quickly.
  static constexpr std::size_t   kCadenceWindowFrames      = 16;
  /// Admissibility floor (us) of the cadence window: faster closes never
  /// presented (bookkeeping); admitting phantoms would fake a huge spread.
  static constexpr std::uint32_t kCadenceMinFrameMicroseconds = 2'000;
  /// Window spread (max-min frame total, us) marking cadence ROUGH: 10 ms is
  /// 60% of a 60 Hz period -- judder a single ordinary outlier cannot fake.
  static constexpr std::uint32_t kCadenceRoughSpreadMicroseconds = 10'000;
  /// Minimum rough evaluations for a CLOSED cadence episode to earn the
  /// [hitch-stutter] annotation: isolated events stay silent, judder speaks.
  static constexpr std::uint64_t kStutterMinEpisodeFrames  = 24;
}
} // namespace GameRules

#endif  // CONSTANTS_H

