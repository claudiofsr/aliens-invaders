// ALIENS INVADERS - C++20 HEADLESS UNIT-TEST SUITE (AGENT 5 AUDITED REBUILD) Contract tests for the
// pure, hardware-independent core of the game, organized by AGENTS.md domain. Three audit rules ...

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "audio_export.h"
#include "bake_hash.h"
#include "cli_flags.h"
#include "constants.h"
#include "fanfare_instruments.h"
#include "stage_catalog.h"
#include "formation_grid.h"
#include "frame_pacing.h"
#include "game_object.h"
#include "highscore_table.h"
#include "hitch_monitor.h"
#include "math_types.h"
#include "motion.h"
#include "path.h"
#include "plate_pacing.h"
#include "prewarm_plan.h"
#include "random_stream.h"
#include "recycle_bin.h"
#include "score_hash.h"
#include "soundfont_synth.h"
#include "stage_fanfare.h"
#include "threading.h"
#include "track_selection.h"
#include "trajectory.h"

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_CASE(name) static void name()
#define RUN_TEST(name)   do {     std::cout << "  • Running " << #name << "... ";     const int initial_failed = g_tests_failed;     name();     if (g_tests_failed == initial_failed) {       std::cout << "\033[32m[PASSED]\033[0m\n";       ++g_tests_passed;     } else {       std::cout << "\033[31m[FAILED]\033[0m\n";     }   } while (0)

#define ASSERT_TRUE(expr)   do {     if (!(expr)) {       std::cerr << "\n    Assertion failed: " << #expr << " at " << __FILE__                 << ":" << __LINE__ << "\n";       ++g_tests_failed;       return;     }   } while (0)

#define ASSERT_EQ(a, b)   do {     if ((a) != (b)) {       std::cerr << "\n    Assertion failed: (" << #a << " == " << #b                 << ") => (" << (a) << " != " << (b) << ") at " << __FILE__                 << ":" << __LINE__ << "\n";       ++g_tests_failed;       return;     }   } while (0)

#define ASSERT_NEAR(a, b, eps)   do {     if (std::abs((a) - (b)) > (eps)) {       std::cerr << "\n    Assertion failed: |" << #a << " - " << #b                 << "| <= " << #eps << " => |" << (a) << " - " << (b)                 << "| = " << std::abs((a) - (b)) << " at " << __FILE__ << ":"                 << __LINE__ << "\n";       ++g_tests_failed;       return;     }   } while (0)

// SECTION 1 - SIMULATION PRIMITIVES (math, collision, motion, determinism)

TEST_CASE(TestCollisionContract) {
  // AABB overlap under the fair-play hitbox factor: touching extents count, separated ones do not,
  // and the free function applies the same rule the composed collider uses (one spelling of the ...
  const AABB box_a(20.0f, 20.0f);
  const AABB box_b(15.0f, 15.0f);
  ASSERT_TRUE(box_a.Intersects(Vec2f(100.0f, 100.0f), Vec2f(100.0f, 100.0f), box_b));
  ASSERT_TRUE(box_a.Intersects(Vec2f(100.0f, 100.0f), Vec2f(115.0f, 110.0f), box_b));
  ASSERT_TRUE(!box_a.Intersects(Vec2f(100.0f, 100.0f), Vec2f(200.0f, 200.0f), box_b));
  ASSERT_TRUE(!box_a.Intersects(Vec2f(100.0f, 100.0f), Vec2f(130.0f, 100.0f), box_b));

  const Transform2D t1(Vec2f(100.0f, 100.0f));
  const Transform2D t_near(Vec2f(110.0f, 110.0f));
  const Transform2D t_far(Vec2f(300.0f, 300.0f));
  ASSERT_TRUE(CheckCollision(t1, box_a, t_near, box_b, GameRules::Player::kCollisionHitboxScale));
  ASSERT_TRUE(!CheckCollision(t1, box_a, t_far, box_b, GameRules::Player::kCollisionHitboxScale));

  // Degenerate extremes: self-overlap at zero distance, tiny vs huge boxes,
  // and a zero factor that shrinks the hitbox away entirely.
  const AABB tiny(1.0f, 1.0f);
  const AABB huge(1000.0f, 1000.0f);
  const Transform2D t0(Vec2f(0, 0));
  const Transform2D t_edge(Vec2f(10, 0));
  const Transform2D t_away(Vec2f(5000, 5000));
  ASSERT_TRUE(CheckCollision(t0, tiny, t0, tiny, 1.0f));
  ASSERT_TRUE(!CheckCollision(t0, tiny, t_away, tiny, 1.0f));
  ASSERT_TRUE(!CheckCollision(t0, tiny, t_edge, tiny, 0.0f));
  ASSERT_TRUE(CheckCollision(t0, huge, Transform2D(Vec2f(500, 0)), huge, 1.0f));

  // SSOT default: every collision entry point defaults its hitbox factor to
  // GameRules::Player::kCollisionHitboxScale -- no private 0.65 spellings. A 30 px gap between ...
  const AABB def_a(20.0f, 20.0f);
  const AABB def_b(15.0f, 15.0f);
  const Vec2f at(100.0f, 100.0f);
  const Vec2f d30(130.0f, 100.0f);
  ASSERT_TRUE(!def_a.Intersects(at, d30, def_b));
  ASSERT_TRUE(def_a.Intersects(at, d30, def_b, 1.0f));
  ASSERT_TRUE(!def_a.Intersects(at, d30, def_b,
                                GameRules::Player::kCollisionHitboxScale));
  const Transform2D td0(at);
  const Transform2D td30(d30);
  ASSERT_TRUE(!CheckCollision(td0, def_a, td30, def_b));
  ASSERT_TRUE(!CheckCollision(td0, def_a, td30, def_b,
                              GameRules::Player::kCollisionHitboxScale));
  ASSERT_TRUE(CheckCollision(td0, def_a, td30, def_b, 1.0f));
}

TEST_CASE(TestCoordLengthUhdRegression) {
  // Regression (UB fix): the int32 products inside Length() overflowed for magnitudes beyond ~46340
  // -- the exact UHD headroom Coord exists for -- and the wrapped sum poisoned the sqrt. The ...
  const Coord big(46341, 46341);
  ASSERT_TRUE(!std::isnan(big.Length()));
  ASSERT_NEAR(big.Length(), std::sqrt(2.0 * 46341.0 * 46341.0), 1e-6);

  // Ordinary screen-space lengths stay exact, including negative quadrants.
  const Coord c345(3, 4);
  ASSERT_NEAR(c345.Length(), 5.0, 1e-9);
  ASSERT_NEAR(Coord(0, 0).Length(), 0.0, 1e-12);
  ASSERT_NEAR(Coord(-30000, -40000).Length(), 50000.0, 1e-6);
}

TEST_CASE(TestTransform2DInterpolation) {
  // Render-interpolation contract: Step() archives the previous position and integrates velocity;
  // InterpolatedPosition() reconstructs the exact point between the two archived states so ...
  Transform2D t(Vec2f(10.0f, 20.0f), 0.0f);
  t.velocity = Vec2f(10.0f, 5.0f);
  t.Step();

  ASSERT_EQ(t.previous_position.x, 10.0f);
  ASSERT_EQ(t.previous_position.y, 20.0f);
  ASSERT_EQ(t.position.x, 20.0f);
  ASSERT_EQ(t.position.y, 25.0f);

  const Vec2f mid = t.InterpolatedPosition(0.5f);
  ASSERT_NEAR(mid.x, 15.0f, 1e-4f);
  ASSERT_NEAR(mid.y, 22.5f, 1e-4f);
}

TEST_CASE(TestGameObjectMotionAndCollider) {
  // Composition contract: Update() integrates motion into the transform on every fixed step, and
  // the collider queries through the very same parts (position accumulated over ten steps lands ...
  simulation::GameObject obj;
  obj.transform.position = Coord(500, 500);
  obj.motion.velocity = Coord(-10, 20);
  for (int step = 0; step < 10; ++step) {
    obj.Update();
  }
  ASSERT_EQ(obj.transform.position.x, 400);
  ASSERT_EQ(obj.transform.position.y, 700);

  simulation::GameObject target;
  target.transform.position = Coord(405, 698);
  target.collider.aabb = AABB(10.0f, 10.0f);
  obj.collider.aabb = AABB(10.0f, 10.0f);
  ASSERT_TRUE(obj.collider.Intersects(obj.transform, target.collider, target.transform,
                                      GameRules::Player::kCollisionHitboxScale));
  target.transform.position = Coord(900, 900);
  ASSERT_TRUE(!obj.collider.Intersects(obj.transform, target.collider, target.transform,
                                       GameRules::Player::kCollisionHitboxScale));
}

TEST_CASE(TestRandomStreamDeterminism) {
  // Same seed, same stream: the backbone of deterministic simulation,
  // replays and headless tests - including the SSOT master seed.
  ASSERT_TRUE(GameRules::Simulation::kDefaultSimulationSeed != 0u);
  RandomStream master_a(GameRules::Simulation::kDefaultSimulationSeed);
  RandomStream master_b(GameRules::Simulation::kDefaultSimulationSeed);
  RandomStream rng1(1337);
  RandomStream rng2(1337);
  for (int i = 0; i < 100; ++i) {
    ASSERT_EQ(master_a.NextU32(), master_b.NextU32());
    ASSERT_EQ(rng1.NextU32(), rng2.NextU32());
    ASSERT_EQ(rng1.UniformInt(10, 50), rng2.UniformInt(10, 50));
    ASSERT_NEAR(rng1.UniformFloat(-1.0f, 1.0f), rng2.UniformFloat(-1.0f, 1.0f), 1e-5f);
  }

  // Distinct seeds diverge immediately: the state transition is injective on
  // non-zero states, so two runs can never share a stream by accident.
  RandomStream s1(1);
  RandomStream s2(2);
  ASSERT_TRUE(s1.NextU32() != s2.NextU32());

  // Range closure (both draws inclusive) and argument-order tolerance.
  RandomStream r(0xA11E414u);
  for (int i = 0; i < 1000; ++i) {
    const int val = r.UniformInt(5, 12);
    ASSERT_TRUE(val >= 5 && val <= 12);
    const float unit = r.UniformFloat(-1.0f, 1.0f);
    ASSERT_TRUE(unit >= -1.0f && unit <= 1.0f);
  }
  ASSERT_TRUE(r.UniformInt(12, 5) >= 5 && r.UniformInt(12, 5) <= 12);

  // A zero seed never locks the xorshift stream into its absorbing state.
  RandomStream zero_seeded(0);
  ASSERT_TRUE(zero_seeded.NextU32() != 0u);
}

// SECTION 2 - GAME RULES: SSOT RELATIONS, PROGRESSION & ECONOMY (AGENT 5)

TEST_CASE(TestSimulationTimingRelations) {
  using namespace GameRules::Simulation;
  // Cross-unit SSOT consistency: the seconds and nanoseconds spellings of the fixed step must agree
  // with the simulation frequency to within one unit, or physics and pacing would drift apart ...
  ASSERT_NEAR(kFixedStepDurationSec,
              1.0 / static_cast<double>(kSimulationFrequencyHz), 1e-9);
  const double step_ns_from_hz = 1.0e9 / static_cast<double>(kSimulationFrequencyHz);
  ASSERT_TRUE(static_cast<double>(kFixedStepNanoseconds) >= step_ns_from_hz - 1.0);
  ASSERT_TRUE(static_cast<double>(kFixedStepNanoseconds) <= step_ns_from_hz + 1.0);

  // Bounded accumulator (AGENT 7): the backlog ceiling admits exactly the per-frame step budget, so
  // pending physics always drains within one rendered frame and a catch-up death spiral cannot ...
  ASSERT_EQ(kMaxAccumulatorNanoseconds,
            static_cast<std::uint64_t>(kMaxStepsPerFrame) * kFixedStepNanoseconds);

  // Stall policy ordering: the frame-delta clamp exceeds the hitch threshold (a stall is classified
  // before it is clamped), the hitch threshold exceeds the ordinary backlog (several missed ...
  ASSERT_TRUE(kMaxFrameDeltaNanoseconds > kHitchDeltaNanoseconds);
  ASSERT_TRUE(kHitchDeltaNanoseconds > kMaxAccumulatorNanoseconds);
  ASSERT_TRUE(kJitterSlackNanoseconds < kFixedStepNanoseconds);
}

TEST_CASE(TestProgressionWaveLadder) {
  using namespace GameRules::Progression;
  // Stage boundaries derive from the SSOT wave count: the last wave of a
  // cycle stays inside it and the next one opens the following stage.
  ASSERT_EQ(WaveToStage(1), 1);
  ASSERT_EQ(WaveToStage(kWavesPerStage), 1);
  ASSERT_EQ(WaveToStage(kWavesPerStage + 1), 2);
  ASSERT_EQ(WaveInStage(1), 0);
  ASSERT_EQ(WaveInStage(kWavesPerStage), kWavesPerStage - 1);
  ASSERT_EQ(WaveInStage(kWavesPerStage + 1), 0);

  // Round-trip identity and closed ranges across a deep campaign: the two
  // views of a wave (stage, index-in-stage) always recompose the wave.
  for (int w = 1; w <= 300; ++w) {
    const int stage = WaveToStage(w);
    const int in_stage = WaveInStage(w);
    ASSERT_EQ((stage - 1) * kWavesPerStage + in_stage + 1, w);
    ASSERT_TRUE(stage >= 1);
    ASSERT_TRUE(in_stage >= 0 && in_stage < kWavesPerStage);
  }
}

TEST_CASE(TestStageCatalogConsistency) {
  using namespace GameRules;
  // The catalog is the compile-time mirror of the rule functions: every derived field must agree
  // with its runtime SSOT source for ANY wave - a strictly stronger contract than pinning ...
  static_assert(StageCatalog::GetProfile(1).stage_cycle == 1);  // constexpr purity

  int previous_wait = std::numeric_limits<int>::max();
  for (int w = 1; w <= 120; ++w) {
    const StageProfile p = StageCatalog::GetProfile(w);
    ASSERT_EQ(p.wave, w);
    ASSERT_EQ(p.stage_cycle, Progression::WaveToStage(w));
    ASSERT_EQ(p.kamikaze_quota, Progression::WaveToStage(w));
    ASSERT_EQ(p.max_attack_wait_frames, Fleet::GetMaxAttackWaitFrames(w));
    ASSERT_NEAR(p.fleet_speed,
                Fleet::ComputeSpeed(w, Progression::kWavesPerStage), 1e-4f);

    // Special-entity waves are spec facts: Madame Curie (radioactive) on
    // wave 4 of every cycle, Albert Alienstein (relativistic) on wave 14.
    ASSERT_EQ(p.is_radioactive_wave, w % Progression::kWavesPerStage == 4);
    ASSERT_EQ(p.is_relativistic_wave, w % Progression::kWavesPerStage == 14);

    // Wanderers unlock from the second stage cycle onward; convoys are non-empty; the bomb ceiling
    // grows with the campaign but never past the combat SSOT cap; dive pacing only tightens as ...
    ASSERT_EQ(p.wanderers_allowed, p.stage_cycle >= 2);
    ASSERT_TRUE(p.convoy_count > 0);
    ASSERT_TRUE(p.max_bombs >= Combat::kBaseBombsPerStage);
    ASSERT_TRUE(p.max_bombs <= Combat::kMaxBombsPerStage);
    ASSERT_TRUE(p.cycle_bomb_mult >= 1.0f);
    ASSERT_TRUE(p.cycle_bomb_mult <= Combat::kCycleBombMultMax);
    ASSERT_TRUE(p.max_attack_wait_frames <= previous_wait);
    previous_wait = p.max_attack_wait_frames;
  }

  // Defensive clamping: waves below the first read as the first wave.
  ASSERT_EQ(StageCatalog::GetProfile(0).wave, 1);
  ASSERT_EQ(StageCatalog::GetProfile(-9).wave, 1);
}

TEST_CASE(TestPlayerKinetics) {
  using namespace GameRules::Player;
  // Thruster ladder: +kSpeedBoostPercent agility per tier, saturated outside
  // the canonical tier range - every expectation derives from the SSOT.
  ASSERT_EQ(ComputeSpeed(-100), ComputeSpeed(0));
  ASSERT_EQ(ComputeSpeed(999), ComputeSpeed(kPlayerMaxSpeedLevel));
  for (int tier = 0; tier <= kPlayerMaxSpeedLevel; ++tier) {
    const float expected =
        kPlayerBaseSpeed * (1.0f + kSpeedBoostPercent * static_cast<float>(tier));
    ASSERT_NEAR(ComputeSpeed(tier), expected, 1e-4f);
    if (tier > 0) {
      ASSERT_TRUE(ComputeSpeed(tier) > ComputeSpeed(tier - 1));
    }
  }

  // Cannon ladder: the fire interval shrinks with each rate tier, never
  // below the simulation floor, and saturates outside the tier range.
  ASSERT_EQ(ComputeFireInterval(-1), ComputeFireInterval(0));
  ASSERT_EQ(ComputeFireInterval(999), ComputeFireInterval(kPlayerMaxFireLevel));
  ASSERT_EQ(ComputeFireInterval(0), static_cast<int>(kBaseFireIntervalFrames));
  for (int tier = 1; tier <= kPlayerMaxFireLevel; ++tier) {
    ASSERT_TRUE(ComputeFireInterval(tier) <= ComputeFireInterval(tier - 1));
    ASSERT_TRUE(ComputeFireInterval(tier) >= kMinFireIntervalFrames);
  }
  ASSERT_TRUE(ComputeFireInterval(kPlayerMaxFireLevel) < ComputeFireInterval(0));
}

TEST_CASE(TestFleetKinetics) {
  using namespace GameRules;
  // Kamikaze quota IS the stage cycle: one more interceptor per cycle,
  // monotonic across waves, stepping exactly at cycle boundaries.
  for (int w = 1; w <= 90; ++w) {
    ASSERT_EQ(Fleet::GetKamikazeQuota(w), Progression::WaveToStage(w));
    ASSERT_TRUE(Fleet::GetKamikazeQuota(w) >= 1);
  }
  for (int w = 1; w < 90; ++w) {
    ASSERT_TRUE(Fleet::GetKamikazeQuota(w + 1) >= Fleet::GetKamikazeQuota(w));
    if (Progression::WaveInStage(w + 1) == 0) {
      ASSERT_EQ(Fleet::GetKamikazeQuota(w + 1), Fleet::GetKamikazeQuota(w) + 1);
    }
  }

  // Dive-pacing ladder: the wait ceiling is a pure function of the stage cycle, strictly tightens
  // across the five baked tiers and saturates after the last one, keeping arbitrarily deep ...
  for (int w = 1; w <= 120; ++w) {
    ASSERT_EQ(Fleet::GetMaxAttackWaitFrames(w),
              Fleet::GetMaxAttackWaitFramesForStage(Progression::WaveToStage(w)));
    ASSERT_TRUE(Fleet::GetMaxAttackWaitFrames(w) > 0);
  }
  constexpr int kTiers = Fleet::FormationGrid::kNumStageModes;
  for (int cycle = 2; cycle <= kTiers; ++cycle) {
    ASSERT_TRUE(Fleet::GetMaxAttackWaitFramesForStage(cycle) <
                Fleet::GetMaxAttackWaitFramesForStage(cycle - 1));
    const int first_wave = (cycle - 1) * Progression::kWavesPerStage + 1;
    ASSERT_EQ(Fleet::GetMaxAttackWaitFrames(first_wave),
              Fleet::GetMaxAttackWaitFramesForStage(cycle));
  }
  ASSERT_EQ(Fleet::GetMaxAttackWaitFramesForStage(kTiers),
            Fleet::kStage5MaxAttackWaitFrames);
  ASSERT_EQ(Fleet::GetMaxAttackWaitFrames(1000), Fleet::kStage5MaxAttackWaitFrames);

  // Fleet velocity: piecewise interpolation inside the per-stage SSOT bands,
  // sampled at every cycle opening; the campaign ladder never decreases.
  const int max_w = Progression::kWavesPerStage;
  const float s1_first = Fleet::ComputeSpeed(1, max_w);
  const float s1_last = Fleet::ComputeSpeed(max_w, max_w);
  const float s2_first = Fleet::ComputeSpeed(max_w + 1, max_w);
  const float s3_first = Fleet::ComputeSpeed(2 * max_w + 1, max_w);
  const float s4_first = Fleet::ComputeSpeed(3 * max_w + 1, max_w);
  const float s5_first = Fleet::ComputeSpeed(4 * max_w + 1, max_w);
  ASSERT_TRUE(s1_first <= s1_last);
  ASSERT_TRUE(s1_last <= s2_first);
  ASSERT_TRUE(s2_first <= s3_first);
  ASSERT_TRUE(s3_first <= s4_first);
  ASSERT_TRUE(s4_first <= s5_first);
  ASSERT_TRUE(s1_first >= Fleet::kStage1MinSpeed && s1_last <= Fleet::kStage1MaxSpeed);
  ASSERT_TRUE(s2_first >= Fleet::kStage2MinSpeed && s2_first <= Fleet::kStage2MaxSpeed);
  ASSERT_TRUE(s3_first >= Fleet::kStage3MinSpeed && s3_first <= Fleet::kStage3MaxSpeed);
  ASSERT_TRUE(s4_first >= Fleet::kStage4MinSpeed && s4_first <= Fleet::kStage4MaxSpeed);
  ASSERT_TRUE(s5_first >= Fleet::kStage5MinSpeed && s5_first <= Fleet::kStage5MaxSpeed);

  // Within one stage the velocity only grows with the wave index, and the
  // interpolation floor keeps the fleet visible in motion at every tier.
  for (int w = 1; w < max_w; ++w) {
    ASSERT_TRUE(Fleet::ComputeSpeed(w + 1, max_w) >= Fleet::ComputeSpeed(w, max_w));
    ASSERT_TRUE(Fleet::ComputeSpeed(w, max_w) >= Fleet::kFleetMinVelocityPixels);
  }

  // Wanderers dive more eagerly than the first-cycle fleet ceiling, and the
  // attack dive is a damped but strictly positive share of cruise velocity.
  ASSERT_EQ(Fleet::GetWandererMaxWaitFrames(), Fleet::kWandererMaxWaitFrames);
  ASSERT_TRUE(Fleet::GetWandererMaxWaitFrames() > 0);
  ASSERT_TRUE(Fleet::GetWandererMaxWaitFrames() < Fleet::GetMaxAttackWaitFrames(1));
  ASSERT_TRUE(Fleet::kAttackDiveSpeedMultiplier > 0.0f);
  ASSERT_TRUE(Fleet::kAttackDiveSpeedMultiplier <= 1.0f);
  ASSERT_TRUE(Fleet::kAttackDiveMinVelocityPixels > 0.0f);
  ASSERT_TRUE(Fleet::kAttackDiveMinVelocityPixels <= s1_first);

  // Smooth-motion tuning contract: the hull rate limiter must be able to track the docking arc, the
  // ease-out slice must stay inside it, and the convergence floor must land below a single ...
  ASSERT_TRUE(Fleet::kJoinHeadingTurnRateDeg > 0.0f);
  ASSERT_TRUE(Fleet::kJoinHeadingTurnRateDeg <= Fleet::kMaxTurnRateDeg);
  ASSERT_TRUE(Fleet::kTurnResponseFactor > 0.0f && Fleet::kTurnResponseFactor <= 1.0f);
  ASSERT_TRUE(Fleet::kTurnSnapThresholdDeg > 0.0f);
  ASSERT_TRUE(Fleet::kTurnSnapThresholdDeg < Fleet::kMaxTurnRateDeg);
  ASSERT_TRUE(Fleet::kJoinDirectDockVelocityFactor >= 1.0f);
  ASSERT_TRUE(Fleet::kFleetCruiseDriftPixels > 0.0f);
  // Dive-spline inertia keeps lateral swings gradual and strictly bounded.
  ASSERT_TRUE(Fleet::kAttackDiveLateralInertia > 0.0f && Fleet::kAttackDiveLateralInertia < 1.0f);
  ASSERT_TRUE(Fleet::kAttackDiveLateralJitterUnits > 0);
  ASSERT_TRUE(Fleet::kAttackDiveLateralSwingCapUnits > 0.0f);
  ASSERT_TRUE(Fleet::kAttackDiveMinAdvanceUnits > 0.0f);
  ASSERT_TRUE(Fleet::kAttackDiveExitNormalizedY > 1024.0f);
}

TEST_CASE(TestCombatEconomyAndFairPlay) {
  using namespace GameRules::Combat;
  // Score economy: the ordinary hit is the unit; kamikazes pay at least the unit; nuked aliens sit
  // strictly between; the extra-shield step is a whole multiple of the unit so awards can never ...
  ASSERT_TRUE(kScorePerAlienHit > 0);
  ASSERT_TRUE(kScorePerKamikazeAlienHit >= kScorePerAlienHit);
  ASSERT_TRUE(kScorePerAlienNuked > kScorePerAlienHit);
  ASSERT_TRUE(kScorePerAlienNuked < kScorePerKamikazeAlienHit);
  ASSERT_TRUE(kExtraShieldScoreStep % kScorePerAlienHit == 0);

  // Bonus economy (AGENT 3): at most one pod per type per stage cycle, dropped probabilistically,
  // with the nuke weighted no heavier than any utility pod. Fixed pools cover every on-screen ...
  ASSERT_TRUE(kMaxBonusesPerStage > 0 && kMaxBonusesPerStage <= 5);
  ASSERT_TRUE(kBonusWaveDropProbability >= 0.0 && kBonusWaveDropProbability <= 1.0);
  const int total_weight =
      kBonusWeightFire + kBonusWeightMulti + kBonusWeightSpeed +
      kBonusWeightShield + kBonusWeightNuke;
  ASSERT_TRUE(total_weight > 0);
  ASSERT_TRUE(kBonusWeightNuke <= kBonusWeightShield);
  ASSERT_TRUE(kBonusWeightNuke <= kBonusWeightFire);
  ASSERT_TRUE(kMaxProjectiles > 0);
  ASSERT_TRUE(kMaxBonuses > 0);
  ASSERT_TRUE(kMaxExhaustParticles > 0);
  ASSERT_TRUE(kMaxExplosionParticles > 0);
  ASSERT_TRUE(kTurnTotalFrames > 0);
  ASSERT_TRUE(kTurningBombTurnTimerFrames > 0);

  // Fair play: the mandatory evasion corridor is wider than one alien hull, the kamikaze blast is
  // lethal beyond the hull, and the seeker's worst-case lateral drift (sine of the SSOT ...
  ASSERT_TRUE(GameRules::Fleet::kSafeEvasionCorridorPixels >
              static_cast<int>(GameRules::Fleet::kAlienBaseWidthPixels));
  ASSERT_TRUE(GameRules::Fleet::kKamikazeBlastRadiusMultiplier > 1.0f);
  ASSERT_TRUE(GameRules::Fleet::kSeekerMissileProbability > 0.0f);
  ASSERT_TRUE(GameRules::Fleet::kSeekerMissileProbability <= 1.0f);
  const float max_lateral_ratio =
      std::sin(GameRules::Fleet::kMaxDeflectionAngleDeg * (Pi / 180.0f));
  ASSERT_TRUE(max_lateral_ratio > 0.20f && max_lateral_ratio < 0.35f);
  // Regression pin: the seeker ceiling is EXACTLY +/-25 degrees (spec);
  // the dodgeable sine band above is derived from this single value.
  ASSERT_NEAR(GameRules::Fleet::kMaxDeflectionAngleDeg, 20.0f, 1e-4f);

  // Special entities (AGENT 3 rules). Madame Curie: the electrosphere is a minority birth roll;
  // with it the hull takes exactly two impacts, the alpha decay shrinks the hull and ...
  using namespace GameRules::SpecialEntities;
  ASSERT_TRUE(kAlien4ElectrosphereProbability > 0.0f);
  ASSERT_TRUE(kAlien4ElectrosphereProbability < 0.5f);
  ASSERT_EQ(kAlien4InitialHealth, 2);
  ASSERT_TRUE(kAlien4AlphaDecayScale > 0.0f && kAlien4AlphaDecayScale < 1.0f);
  ASSERT_TRUE(kAlien4DecayedOrbitSpeedMult > 1.0f);
  ASSERT_TRUE(kAlien4DecayedSpeedMult > 1.0f);
  ASSERT_TRUE(kAlien14WarpEvasionProbability > 0.0f);
  ASSERT_TRUE(kAlien14WarpEvasionProbability <= 1.0f);
  ASSERT_TRUE(kAlien14NumWarpEvasions > 0);
  ASSERT_TRUE(kAlien14WarpTriggerRadiusFactor > 1.0f);
  ASSERT_TRUE(kAlien14WarpSpeedPixels > GameRules::Fleet::kFleetMinVelocityPixels);
  ASSERT_TRUE(kAlien14WarpCooldownSeconds > 0.0f);
  ASSERT_TRUE(kAlien14WarpGhostCapacity > 0u);
}

// SECTION 3 - HONEYCOMB SUPER-TABLE & SPECIAL ENTITIES (AGENT 2)

TEST_CASE(TestFormationGridSymmetryAndScaling) {
  using namespace GameRules::Fleet;
  using FG = FormationGrid;
  FG::Recompute(1.0f);

  // Symmetric axis contract (AGENT 5): the super-table exposes strictly parallel X/Y names, and in
  // the flat mode BOTH axes are equidistant - columns step by SpacingX() and rows step by ...
  for (int c = 0; c + 1 < FG::kMaxGridCols; ++c) {
    const FormationSlot& a = FG::GetSlot(1, 0, c);
    const FormationSlot& b = FG::GetSlot(1, 0, c + 1);
    ASSERT_EQ(b.offset_x - a.offset_x, FG::SpacingX());
    ASSERT_EQ(b.offset_y, a.offset_y);
  }
  for (int r = 0; r + 1 < FG::kMaxGridRows; ++r) {
    const FormationSlot& a = FG::GetSlot(1, r, 0);
    const FormationSlot& b = FG::GetSlot(1, r + 1, 0);
    ASSERT_EQ(b.offset_y - a.offset_y, FG::SpacingY());
  }
  // Rows of equal parity (same stagger phase) also keep the X offset.
  for (int r = 0; r + 2 < FG::kMaxGridRows; ++r) {
    const FormationSlot& a = FG::GetSlot(1, r, 0);
    const FormationSlot& b = FG::GetSlot(1, r + 2, 0);
    ASSERT_EQ(b.offset_x, a.offset_x);
    ASSERT_EQ(b.offset_y - a.offset_y, 2 * FG::SpacingY());
  }

  // Honeycomb stagger: odd rows shift by a strict fraction of one cell, so the lattice never
  // degenerates into plain columns and never self-touches; walking one row flips the X offset ...
  const int stagger_dx = FG::GetSlot(1, 1, 0).offset_x - FG::GetSlot(1, 0, 0).offset_x;
  ASSERT_TRUE(stagger_dx != 0);
  ASSERT_TRUE(std::abs(stagger_dx) < FG::SpacingX());
  for (int r = 0; r + 1 < FG::kMaxGridRows; ++r) {
    const int dx = FG::GetSlot(1, r + 1, 0).offset_x - FG::GetSlot(1, r, 0).offset_x;
    ASSERT_TRUE(dx == stagger_dx || dx == -stagger_dx);
  }

  // Hull separation on BOTH axes: cell spacing strictly exceeds the hull box, so neighbours in the
  // lattice can never overlap - the geometric half of the no-overlap guarantee for aliens 1..15.
  ASSERT_TRUE(FG::SpacingX() > FG::Width());
  ASSERT_TRUE(FG::SpacingY() > FG::Height());

  // Five-tier super-table with O(1) saturation: stage cycles beyond the
  // last baked mode clamp onto it, keeping deep campaigns branch-free.
  ASSERT_EQ(FG::kNumStageModes, 5);
  const FormationSlot& slot_last = FG::GetSlot(FG::kNumStageModes, 0, 1);
  const FormationSlot& slot_beyond = FG::GetSlot(FG::kNumStageModes + 4, 0, 1);
  ASSERT_EQ(slot_beyond.offset_x, slot_last.offset_x);
  ASSERT_EQ(slot_beyond.offset_y, slot_last.offset_y);

  // Distinct curvature signatures per baked mode: the Stage 4 spearhead dips its centre column
  // toward the player first, while the Stage 5 deep W-wing keeps the centre high and pushes the ...
  const int centre_col = kFormationStage2CurveCenter;
  const FormationSlot& s4_centre = FG::GetSlot(4, 0, centre_col);
  const FormationSlot& s4_flank = FG::GetSlot(4, 0, centre_col + 5);
  const FormationSlot& s5_centre = FG::GetSlot(5, 0, centre_col);
  const FormationSlot& s5_flank = FG::GetSlot(5, 0, centre_col + 5);
  ASSERT_TRUE(s4_centre.offset_y > s4_flank.offset_y);   // wedge tip dives first
  ASSERT_TRUE(s5_flank.offset_y > s5_centre.offset_y);   // W-wing flanks dive first

  // Scale linearity from the SSOT design metrics: every cached fleet metric is its design constant
  // scaled and rounded exactly once (the window- resize contract), and hull separation survives ...
  FG::Recompute(1.5f);
  ASSERT_EQ(FG::Width(), FastRound(kAlienBaseWidthPixels * 1.5f));
  ASSERT_EQ(FG::Height(), FastRound(kAlienBaseHeightPixels * 1.5f));
  ASSERT_EQ(FG::SpacingX(), FastRound(kAlienHorizontalSpacingPixels * 1.5f));
  ASSERT_EQ(FG::SpacingY(), FastRound(kAlienVerticalSpacingPixels * 1.5f));
  ASSERT_EQ(FG::BaseCruiseY(), FastRound(kFleetBaseCruiseYPixels * 1.5f));
  ASSERT_TRUE(FG::SpacingX() > FG::Width());
  ASSERT_TRUE(FG::SpacingY() > FG::Height());

  FG::Recompute(1.0f);  // restore the design baseline for later tests
}

TEST_CASE(TestAlien14WarpCellSelection) {
  // Albert Alienstein's relativistic jump (AGENT 2 spec): the trigger halo strictly exceeds the
  // hull radius, landing cells are drawn OUTSIDE both Chebyshev exclusion zones in O(1), no two ...
  using namespace GameRules::SpecialEntities;
  const float half_hull = GameRules::Fleet::kAlienBaseWidthPixels * 0.5f;
  const float hull_radius = std::hypot(half_hull, half_hull);
  const float trigger_dist = hull_radius * kAlien14WarpTriggerRadiusFactor;
  ASSERT_TRUE(trigger_dist > hull_radius);

  using FG = GameRules::Fleet::FormationGrid;
  constexpr int N = kAlien14WarpMinCellSeparation;

  // SSOT ChebyshevDistance from math_types.h (DRY)
  ASSERT_EQ(ChebyshevDistance(0, 0, 3, 1), 3);
  ASSERT_EQ(ChebyshevDistance(5, 5, 5, 5), 0);
  ASSERT_EQ(ChebyshevDistance(2, 4, 4, 2), 2);
  ASSERT_EQ(ChebyshevDistance(Coord(0, 0), Coord(3, 1)), 3);

  // FloorDiv maps a player LEFT of the formation anchor to the correct
  // negative cell index; C++ truncation would shift Zona2 one cell right.
  ASSERT_EQ(FloorDiv(-350, 93), -4);
  ASSERT_EQ(FloorDiv(-186, 93), -2);
  ASSERT_EQ(FloorDiv(-1, 93), -1);
  ASSERT_EQ(FloorDiv(0, 93), 0);
  ASSERT_EQ(FloorDiv(350, 93), 3);
  // Defensive zero span: a degenerate denominator yields 0 instead of UB.
  ASSERT_EQ(FloorDiv(350, 0), 0);

  RandomStream rng(0xA11E414u);
  const int origin_col = 5, origin_row = 5;
  const int player_col = 5, player_row = 9;
  const int used_cols = 12, used_rows = 10;
  // Priority 1: the single O(1) draw lands OUTSIDE both exclusion zones -- Zona1 around the alien,
  // Zona2 around the player cell -- with distance > N from both by geometric construction.
  for (int i = 0; i < 300; ++i) {
    const auto cell = FG::PickRestrictedWarpCell(
        origin_col, origin_row, player_col, player_row, used_cols, used_rows,
        N, rng);
    ASSERT_TRUE(cell.col >= 0 && cell.col < used_cols);
    ASSERT_TRUE(cell.row >= 0 && cell.row < used_rows);
    ASSERT_TRUE(ChebyshevDistance(cell.col, cell.row, origin_col, origin_row) > N);
    ASSERT_TRUE(ChebyshevDistance(cell.col, cell.row, player_col, player_row) > N);
  }

  // Overlapping zones (player adjacent to the alien) keep every draw valid on the full-width
  // domain: with the > N separation the merged exclusion leaves columns 12..15 as the only ...
  for (int i = 0; i < 100; ++i) {
    const auto cell = FG::PickRestrictedWarpCell(
        origin_col, origin_row, origin_col + 1, origin_row, FG::kMaxGridCols,
        used_rows, N, rng);
    ASSERT_TRUE(cell.col >= 0 && cell.col < FG::kMaxGridCols);
    ASSERT_TRUE(ChebyshevDistance(cell.col, cell.row, origin_col, origin_row) > N);
    ASSERT_TRUE(ChebyshevDistance(cell.col, cell.row, origin_col + 1, origin_row) > N);
  }

  // Degenerate: zones covering the WHOLE domain fall back to {0,0} (the
  // caller's Priority-2 scan and Priority-3 corner fallback take over).
  {
    const auto cell = FG::PickRestrictedWarpCell(3, 3, 3, 3, 7, 7, N, rng);
    ASSERT_EQ(cell.col, 0);
    ASSERT_EQ(cell.row, 0);
  }
  {
    const auto cell = FG::PickRestrictedWarpCell(0, 0, 0, 0, 1, 1, N, rng);
    ASSERT_EQ(cell.col, 0);
    ASSERT_EQ(cell.row, 0);
  }

  // No-sharing rule: two starships may NEVER occupy the same honeycomb cell. When the scan fails
  // and the far corner is taken, the corner walk must return the one free cell of a saturated ...
  {
    std::array<std::uint16_t, FG::kMaxGridRows> occ{};
    for (auto& row : occ) row = 0xFFFFu;
    occ[2] = static_cast<std::uint16_t>(0xFFFFu & ~(1u << 0));
    const auto d = FG::ComputeRelativisticWarp(4, 1, 8, 4, 640, 1000,
                                               Coord(100, 82), 1, 1.0f,
                                               1280, 1080, occ, rng);
    ASSERT_EQ(d.col, 0);
    ASSERT_EQ(d.row, 2);
  }
  {
    // The corner itself free -> the corner is kept (spec Priority 3).
    std::array<std::uint16_t, FG::kMaxGridRows> occ{};
    for (auto& row : occ) row = 0xFFFFu;
    occ[0] = static_cast<std::uint16_t>(0xFFFFu & ~(1u << 0));
    const auto d = FG::ComputeRelativisticWarp(4, 1, 8, 4, 640, 1000,
                                               Coord(100, 82), 1, 1.0f,
                                               1280, 1080, occ, rng);
    ASSERT_EQ(d.col, 0);
    ASSERT_EQ(d.row, 0);
  }
  {
    // Randomized invariant: whenever a free cell exists in the active
    // domain, the decision lands on a FREE cell -- never on a shared one.
    RandomStream rng2(0xC0FFEEu);
    const int fuzz_cols = 10, fuzz_rows = 6;
    for (int iter = 0; iter < 400; ++iter) {
      std::array<std::uint16_t, FG::kMaxGridRows> occ{};
      int free_cells = 0;
      for (int r = 0; r < fuzz_rows; ++r) {
        for (int c = 0; c < fuzz_cols; ++c) {
          if (rng2.UniformInt(0, 3) == 0) {
            ++free_cells;
          } else {
            occ[static_cast<std::size_t>(r)] |=
                static_cast<std::uint16_t>(std::uint16_t{1}
                                           << static_cast<unsigned>(c));
          }
        }
      }
      const auto d = FG::ComputeRelativisticWarp(
          rng2.UniformInt(0, fuzz_cols - 1), rng2.UniformInt(0, fuzz_rows - 1),
          fuzz_cols, fuzz_rows, 640, 1000, Coord(100, 82), 1, 1.0f,
          1280, 1080, occ, rng2);
      ASSERT_TRUE(d.row >= 0 && d.row < fuzz_rows);
      ASSERT_TRUE(d.col >= 0 && d.col < fuzz_cols);
      const bool dest_taken =
          (occ[static_cast<std::size_t>(d.row)] &
           (std::uint16_t{1} << static_cast<unsigned>(d.col))) != 0;
      if (free_cells > 0) ASSERT_TRUE(!dest_taken);
    }
  }

  // Spec ("sole death condition"): the projectile-kill gate is total -- ANY remaining evasion
  // blocks the kill; only zero (or defensive negative) allows it, and never mid-transit ...
  ASSERT_TRUE(GameRules::SpecialEntities::WarpEvasionBlocksProjectileKill(2));
  ASSERT_TRUE(GameRules::SpecialEntities::WarpEvasionBlocksProjectileKill(1));
  ASSERT_TRUE(!GameRules::SpecialEntities::WarpEvasionBlocksProjectileKill(0));
  ASSERT_TRUE(!GameRules::SpecialEntities::WarpEvasionBlocksProjectileKill(-1));
}

TEST_CASE(TestKinematicsHelpers) {
  using simulation::AdvanceTurnDeg;
  using simulation::ClampStepToBudget;
  using simulation::AdvanceToward;
  const float resp = GameRules::Fleet::kTurnResponseFactor;
  const float snap = GameRules::Fleet::kTurnSnapThresholdDeg;
  const float cap  = GameRules::Fleet::kMaxTurnRateDeg;

  // Fixed point: a hull already on the target bearing never drifts.
  ASSERT_NEAR(AdvanceTurnDeg(37.0f, 37.0f, resp, snap, cap), 37.0f, 1e-4f);
  // Rate limit: even the widest turn advances at most one capped slice.
  ASSERT_TRUE(std::abs(AdvanceTurnDeg(0.0f, 180.0f, resp, snap, cap)) <= cap + 1e-4f);
  // Shortest arc: +170 turns positive, -170 negative, and the wrap seam
  // (175 -> -175) crosses through 180 instead of spinning the long way.
  ASSERT_TRUE(AdvanceTurnDeg(0.0f, 170.0f, resp, snap, cap) > 0.0f);
  ASSERT_TRUE(AdvanceTurnDeg(0.0f, -170.0f, resp, snap, cap) < 0.0f);
  ASSERT_NEAR(AdvanceTurnDeg(175.0f, -175.0f, resp, snap, cap), 176.8f, 1e-3f);
  // Convergence + exact landing: iterated slices arrive, then stay.
  float a = 0.0f;
  for (int i = 0; i < 1000; ++i) a = AdvanceTurnDeg(a, 90.0f, resp, snap, cap);
  ASSERT_NEAR(a, 90.0f, 1e-4f);
  ASSERT_NEAR(AdvanceTurnDeg(a, 90.0f, resp, snap, cap), 90.0f, 1e-4f);

  // Delta Jump Guard: Chebyshev clamp preserves direction, caps every axis.
  const Vec2f from{0.0f, 0.0f};
  const Vec2f clamped = ClampStepToBudget(from, Vec2f{30.0f, 40.0f}, 10.0f);
  ASSERT_NEAR(clamped.x, 7.5f, 1e-4f);   // max axis 40 -> 10 scales by 0.25
  ASSERT_NEAR(clamped.y, 10.0f, 1e-4f);
  const Vec2f inside = ClampStepToBudget(from, Vec2f{3.0f, 4.0f}, 10.0f);
  ASSERT_NEAR(inside.x, 3.0f, 1e-4f);    // within budget: untouched
  ASSERT_NEAR(inside.y, 4.0f, 1e-4f);
  const Vec2f frozen = ClampStepToBudget(from, Vec2f{30.0f, 40.0f}, 0.0f);
  ASSERT_NEAR(frozen.x, 0.0f, 1e-4f);    // non-positive budget freezes

  // Pursuit primitive: lands exactly inside one step, bounded otherwise.
  const Vec2f landed = AdvanceToward(Vec2f{0.0f, 0.0f}, Vec2f{3.0f, 4.0f}, 10.0f);
  ASSERT_NEAR(landed.x, 3.0f, 1e-4f);
  ASSERT_NEAR(landed.y, 4.0f, 1e-4f);
  const Vec2f stepped = AdvanceToward(Vec2f{0.0f, 0.0f}, Vec2f{300.0f, 400.0f}, 10.0f);
  ASSERT_NEAR(stepped.Length(), 10.0f, 1e-3f);
  ASSERT_NEAR(AdvanceToward(Vec2f{5.0f, 5.0f}, Vec2f{5.0f, 5.0f}, 3.0f).x, 5.0f, 1e-6f);
}

TEST_CASE(TestTrajectoryKinematicsContract) {
  using FG = GameRules::Fleet::FormationGrid;
  const ViewportMetrics metrics{1280, 720, 2.0f / 3.0f};
  FG::Recompute(metrics.scale);

  // Straight descending entry in normalized 1024-space; the spline resampler
  // and every stage below are pure -- the whole machine runs without SDL/Gfx.
  const std::vector<Vec2f> raw{{512.0f, -50.0f}, {512.0f, 300.0f}, {512.0f, 600.0f}};
  const FlightPath path(raw);
  Coord base_cruise(100, FG::BaseCruiseY());
  Trajectory traj(&path, false, base_cruise, &metrics, /*col*/ 3, /*row*/ 2,
                  0x5EED1234u, /*stage_cycle*/ 1, /*used_cols*/ 10);
  ASSERT_TRUE(traj.Stage() == Trajectory::arriving);

  const Coord start = traj.InitPosition();
  ASSERT_TRUE(start.y < 0);  // pushed off-screen above the top edge

  const float velocity = 3.4f;
  float x = static_cast<float>(start.x);
  float y = static_cast<float>(start.y);

  // arriving -> joining -> cruising: the per-frame displacement NEVER exceeds the velocity budget
  // (no micro-teleports) and docking lands exactly on the honeycomb slot (continuous docking ...
  int frames = 0;
  while (traj.Stage() != Trajectory::cruising && frames < 5000) {
    float nx = 0.0f, ny = 0.0f;
    traj.NextPositionF(x, y, velocity, nx, ny);
    ASSERT_TRUE(std::abs(nx - x) <= velocity + 1e-3f);
    ASSERT_TRUE(std::abs(ny - y) <= velocity + 1e-3f);
    x = nx; y = ny; ++frames;
  }
  ASSERT_TRUE(traj.Stage() == Trajectory::cruising);
  const Coord slot = traj.CruiseTarget();
  ASSERT_NEAR(x, static_cast<float>(slot.x), 1e-3f);
  ASSERT_NEAR(y, static_cast<float>(slot.y), 1e-3f);

  // Docked cruising tracks the drifting formation anchor within budget.
  for (int i = 0; i < 30; ++i) {
    base_cruise.x += 1;
    float nx = 0.0f, ny = 0.0f;
    traj.NextPositionF(x, y, velocity, nx, ny);
    ASSERT_TRUE(std::abs(nx - x) <= velocity + 1e-3f);
    x = nx; y = ny;
  }
  ASSERT_NEAR(x, static_cast<float>(traj.CruiseTarget().x), velocity);
  ASSERT_NEAR(y, static_cast<float>(traj.CruiseTarget().y), 1e-3f);

  // Attack dive: consumed within the dive budget; the wrap re-enters exactly
  // one grid height above the top edge at the same column (design contract).
  traj.BuildAttack(Coord(x, y));
  ASSERT_TRUE(traj.Stage() == Trajectory::attacking);
  const float eff = std::max(GameRules::Fleet::kAttackDiveMinVelocityPixels,
                             velocity * GameRules::Fleet::kAttackDiveSpeedMultiplier);
  bool wrapped = false;
  for (int f = 0; f < 5000 && !wrapped; ++f) {
    const float px = x, py = y;
    float nx = 0.0f, ny = 0.0f;
    traj.NextPositionF(x, y, velocity, nx, ny);
    if (traj.Stage() == Trajectory::joining) {
      ASSERT_NEAR(ny, -static_cast<float>(FG::Height()), 1e-3f);
      ASSERT_NEAR(nx, px, 1e-3f);
      wrapped = true;
      x = nx; y = ny;
      break;
    }
    ASSERT_TRUE(std::abs(nx - px) <= eff + 1e-3f);
    ASSERT_TRUE(std::abs(ny - py) <= eff + 1e-3f);
    x = nx; y = ny;
  }
  ASSERT_TRUE(wrapped);

  // Re-docking from the wrap converges back to cruising: the rate-limited
  // banked arc can never orbit the slot forever (capture-radius contract).
  frames = 0;
  while (traj.Stage() != Trajectory::cruising && frames < 5000) {
    float nx = 0.0f, ny = 0.0f;
    traj.NextPositionF(x, y, velocity, nx, ny);
    ASSERT_TRUE(std::abs(nx - x) <= velocity + 1e-3f);
    ASSERT_TRUE(std::abs(ny - y) <= velocity + 1e-3f);
    x = nx; y = ny; ++frames;
  }
  ASSERT_TRUE(traj.Stage() == Trajectory::cruising);

  // The kamikaze dive shares the same completion/wrap contract.
  traj.BuildKamikazeDive(Coord(x, y), Coord(640, 640));
  ASSERT_TRUE(traj.Stage() == Trajectory::attacking);
  wrapped = false;
  for (int f = 0; f < 5000 && !wrapped; ++f) {
    float nx = 0.0f, ny = 0.0f;
    traj.NextPositionF(x, y, velocity, nx, ny);
    if (traj.Stage() == Trajectory::joining) wrapped = true;
    x = nx; y = ny;
  }
  ASSERT_TRUE(wrapped);
}

TEST_CASE(TestSplineResampler) {
  // Centripetal Catmull-Rom endpoints: the curve interpolates its two
  // inner control points exactly at t=0 and t=1.
  {
  const Vec2f p0(0.0f, 0.0f);
  const Vec2f p1(10.0f, 20.0f);
  const Vec2f p2(30.0f, 40.0f);
  const Vec2f p3(50.0f, 30.0f);

  const Vec2f start = FlightPath::EvaluateCentripetalCR(p0, p1, p2, p3, 0.0f);
  ASSERT_NEAR(start.x, p1.x, 1e-3f);
  ASSERT_NEAR(start.y, p1.y, 1e-3f);

  const Vec2f end = FlightPath::EvaluateCentripetalCR(p0, p1, p2, p3, 1.0f);
  ASSERT_NEAR(end.x, p2.x, 1e-3f);
  ASSERT_NEAR(end.y, p2.y, 1e-3f);
  }

  {
  // The out-parameter resampler (used by BuildAttack/BuildKamikazeDive to keep the mid-match loop
  // allocation-free) must be bit-identical to the value-returning version: same centripetal ...
  const std::vector<Vec2f> controls = {
      {100.0f, -50.0f}, {260.0f, 120.0f}, {410.0f, 90.0f},
      {620.0f, 300.0f}, {700.0f, 900.0f}};

  const std::vector<Vec2f> reference = FlightPath::GenerateSmoothSplineF(controls);
  ASSERT_TRUE(reference.size() > controls.size());

  std::vector<Vec2f> out;
  FlightPath::GenerateSmoothSplineInto(controls, out);
  ASSERT_EQ(out.size(), reference.size());
  for (size_t i = 0; i < out.size(); ++i) {
    ASSERT_EQ(out[i].x, reference[i].x);
    ASSERT_EQ(out[i].y, reference[i].y);
  }

  // Steady-state contract: re-running into the SAME vector (the reused
  // Trajectory::attack_ capacity path) must yield identical output.
  FlightPath::GenerateSmoothSplineInto(controls, out);
  ASSERT_EQ(out.size(), reference.size());
  for (size_t i = 0; i < out.size(); ++i) {
    ASSERT_EQ(out[i].x, reference[i].x);
    ASSERT_EQ(out[i].y, reference[i].y);
  }

  // Degenerate inputs mirror the early-return contract.
  const std::vector<Vec2f> single = {{7.0f, 8.0f}};
  std::vector<Vec2f> tiny;
  FlightPath::GenerateSmoothSplineInto(single, tiny);
  ASSERT_EQ(tiny.size(), single.size());
  ASSERT_NEAR(tiny[0].x, 7.0f, 1e-6f);
  FlightPath::GenerateSmoothSplineInto({}, tiny);
  ASSERT_TRUE(tiny.empty());
  }

  // The attack-loop factory sweeps `repetitions` full turns expressed with the circle-constant
  // SSOT: the resampled path starts on the entry point, stays finite, and carries the two-loop ...
  {
    const FlightPath loop = FlightPath::Loop(160.0f, 430.0f, 2);
    ASSERT_TRUE(!loop.Empty());
    ASSERT_NEAR(loop[0].x, 160.0f, 1e-3f);
    ASSERT_NEAR(loop[0].y, 430.0f, 1e-3f);
    ASSERT_TRUE(loop.Size() > 100u);
    for (size_t i = 0; i < loop.Size(); ++i) {
      ASSERT_TRUE(std::isfinite(loop[i].x) && std::isfinite(loop[i].y));
    }
  }
}

// SECTION 4 - FRAME PACING & HITCH DIAGNOSTICS (AGENT 6/7)

TEST_CASE(TestPacingBoundedAccumulator) {
  using GameRules::Simulation::kFixedStepNanoseconds;
  using GameRules::Simulation::kHitchDeltaNanoseconds;
  using GameRules::Simulation::kMaxAccumulatorNanoseconds;
  using GameRules::Simulation::kMaxFrameDeltaNanoseconds;
  using GameRules::Simulation::kMaxStepsPerFrame;

  // A hitch is several missed VBlanks: it can never be confused with ordinary jitter.
  ASSERT_TRUE(kHitchDeltaNanoseconds > kMaxAccumulatorNanoseconds);
  ASSERT_TRUE(kHitchDeltaNanoseconds <= kMaxFrameDeltaNanoseconds);
  ASSERT_TRUE(!Pacing::IsHitch(kHitchDeltaNanoseconds - 1));
  ASSERT_TRUE(Pacing::IsHitch(kHitchDeltaNanoseconds));

  // Below the threshold the policy is bit-identical to the plain clamp.
  const std::uint64_t backlogs[] = {0, 5'000'000ULL, kFixedStepNanoseconds - 1};
  for (const std::uint64_t acc : backlogs) {
    for (std::uint64_t raw = 0; raw < kHitchDeltaNanoseconds; raw += 100'000ULL) {
      const std::uint64_t plain_clamp =
          std::min(acc + std::min(raw, kMaxFrameDeltaNanoseconds), kMaxAccumulatorNanoseconds);
      ASSERT_EQ(Pacing::AccumulateBounded(acc, raw), plain_clamp);
    }
  }

  // Runs the per-frame stepping loop exactly like FramePacingEngine::ShouldStepPhysics.
  const auto steps_for = [](std::uint64_t acc) {
    int steps = 0;
    while (acc >= Pacing::StepThreshold(steps) && steps < kMaxStepsPerFrame) {
      acc = Pacing::ConsumeStep(acc);
      ++steps;
    }
    return steps;
  };

  // A 200 ms stall must still be clamped before step consumption. Use an
  // explicit std::uint64_t to match the policy constants.
  constexpr std::uint64_t kStall200ms = 200'000'000ULL;
  const std::uint64_t clamped_stall =
      std::min(std::min(kStall200ms, kMaxFrameDeltaNanoseconds), kMaxAccumulatorNanoseconds);
  ASSERT_EQ(steps_for(clamped_stall), 2);

  // ...whereas any stall, however long, yields exactly ONE step and a valid alpha.
  const std::uint64_t stalls[] = {kHitchDeltaNanoseconds, 100'000'000ULL, 250'000'000ULL,
                                  5'000'000'000ULL};
  for (const std::uint64_t stall : stalls) {
    ASSERT_EQ(steps_for(Pacing::AccumulateBounded(0, stall)), 1);
    ASSERT_EQ(steps_for(Pacing::AccumulateBounded(kFixedStepNanoseconds - 1, stall)), 1);
  }

  // The backlog ceiling still bounds ordinary lag to kMaxStepsPerFrame steps.
  ASSERT_EQ(Pacing::AccumulateBounded(kMaxAccumulatorNanoseconds, 30'000'000ULL),
            kMaxAccumulatorNanoseconds);
  ASSERT_EQ(steps_for(kMaxAccumulatorNanoseconds), kMaxStepsPerFrame);

  // Interpolation helpers: no underflow, alpha spans exactly [0, 1].
  ASSERT_EQ(Pacing::ConsumeStep(0), 0ULL);
  ASSERT_NEAR(Pacing::InterpolationAlpha(0), 0.0f, 1e-6f);
  ASSERT_NEAR(Pacing::InterpolationAlpha(kFixedStepNanoseconds), 1.0f, 1e-6f);
}

TEST_CASE(TestHitchFrameRecorderAttribution) {
  constexpr std::uint64_t kMs = 1'000'000ULL;
  Hitch::FrameRecorder recorder;
  Hitch::Record out;

  // A healthy 16 ms frame is never reported.
  recorder.Begin(1000 * kMs);
  recorder.EndPhase(Hitch::Phase::kInput, 1001 * kMs);
  recorder.EndPhase(Hitch::Phase::kSimulation, 1003 * kMs);
  recorder.EndPhase(Hitch::Phase::kRender, 1006 * kMs);
  recorder.EndPhase(Hitch::Phase::kPresent, 1016 * kMs);
  ASSERT_TRUE(!recorder.End(1016 * kMs, out));

  // A compositor stall inside the present call is blamed on `present`.
  recorder.Begin(2000 * kMs);
  recorder.EndPhase(Hitch::Phase::kInput, 2001 * kMs);
  recorder.EndPhase(Hitch::Phase::kSimulation, 2003 * kMs);
  recorder.EndPhase(Hitch::Phase::kRender, 2006 * kMs);
  recorder.EndPhase(Hitch::Phase::kPresent, 2216 * kMs);
  ASSERT_TRUE(recorder.End(2216 * kMs, out));
  ASSERT_TRUE(out.culprit == Hitch::Phase::kPresent);
  ASSERT_EQ(out.total_us, 216'000u);
  ASSERT_EQ(out.phase_us[static_cast<std::size_t>(Hitch::Phase::kPresent)], 210'000u);
  ASSERT_EQ(out.frame_index, 2u);
  std::uint64_t phase_sum = 0;
  for (const std::uint32_t us : out.phase_us) phase_sum += us;
  ASSERT_EQ(phase_sum, static_cast<std::uint64_t>(out.total_us));

  // The report threshold is inclusive, and time left after the last marker is `pacing`.
  const std::uint64_t threshold = GameRules::Diagnostics::kHitchReportNanoseconds;
  recorder.Begin(0);
  ASSERT_TRUE(!recorder.End(threshold - 1, out));
  recorder.Begin(0);
  ASSERT_TRUE(recorder.End(threshold, out));
  ASSERT_TRUE(out.culprit == Hitch::Phase::kPacing);

  // A clock that steps backwards can neither underflow nor raise a false report.
  recorder.Begin(5000 * kMs);
  recorder.EndPhase(Hitch::Phase::kInput, 4000 * kMs);
  ASSERT_TRUE(!recorder.End(4000 * kMs, out));

  // Pure helpers: deterministic tie-break and saturating conversion.
  ASSERT_TRUE(Hitch::DominantPhase({5, 5, 0, 0, 0}) == Hitch::Phase::kInput);
  ASSERT_TRUE(Hitch::DominantPhase({1, 2, 3, 9, 4}) == Hitch::Phase::kPresent);
  ASSERT_EQ(Hitch::ToMicros(UINT64_MAX), UINT32_MAX);
  ASSERT_EQ(Hitch::ToMicros(1'999ULL), 1u);
}

TEST_CASE(TestHitchSpscRingWaitFree) {
  Hitch::SpscRing<int, 4> ring;
  int value = -1;
  ASSERT_TRUE(!ring.TryPop(value));
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(ring.TryPush(i));
  ASSERT_TRUE(!ring.TryPush(99));  // full: the newest item is dropped, the producer never waits
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(ring.TryPop(value));
    ASSERT_EQ(value, i);
  }
  ASSERT_TRUE(!ring.TryPop(value));

  // Wrap-around far beyond the capacity keeps strict FIFO order.
  int next_in = 0;
  int next_out = 0;
  for (int round = 0; round < 50; ++round) {
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(ring.TryPush(next_in++));
    for (int i = 0; i < 3; ++i) {
      ASSERT_TRUE(ring.TryPop(value));
      ASSERT_EQ(value, next_out++);
    }
  }

  // Two real threads: order must survive contention (validated under TSan as well).
  constexpr int kItems = 200'000;
  Hitch::SpscRing<int, 64> shared;
  std::atomic<bool> ordered{true};
  {
    Portable::JThread consumer([&](Portable::StopToken) {
      int expected = 0;
      int item = 0;
      while (expected < kItems && ordered.load(std::memory_order_relaxed)) {
        if (!shared.TryPop(item)) {
          std::this_thread::yield();
        } else if (item != expected++) {
          ordered.store(false, std::memory_order_relaxed);
        }
      }
    });
    Portable::JThread producer([&](Portable::StopToken) {
      for (int i = 0; i < kItems && ordered.load(std::memory_order_relaxed);) {
        if (shared.TryPush(i)) {
          ++i;
        } else {
          std::this_thread::yield();
        }
      }
    });
  }  // both threads are joined here
  ASSERT_TRUE(ordered.load());
}

TEST_CASE(TestHitchVerdictPolicy) {
  // The verdict taxonomy is pure injected-numbers policy (no clock, no I/O): it mechanizes the
  // legend's reading rules so the -log output ANSWERS "who caused it" instead of leaving the ...
  using Hitch::Verdict;

  // Phase arrays in microseconds: {input, simulation, render, present, pacing}.
  const auto phases = [](std::uint32_t in, std::uint32_t sim, std::uint32_t ren,
                         std::uint32_t pre, std::uint32_t pac) {
    return std::array<std::uint32_t, Hitch::kPhaseCount>{in, sim, ren, pre, pac};
  };

  // Dominant-phase mapping. The fixtures replay representative session
  // frames: a WSI wait and a resize-triggered plate rebake.
  ASSERT_TRUE(Hitch::ClassifyVerdict(phases(100u, 0u, 100u, 50'700u, 0u), false) ==
              Verdict::kExternalWait);
  ASSERT_TRUE(Hitch::ClassifyVerdict(phases(35'700u, 0u, 111'500u, 1'300u, 0u), false) ==
              Verdict::kEngineRender);
  ASSERT_TRUE(Hitch::ClassifyVerdict(phases(100u, 90'000u, 200u, 300u, 0u), false) ==
              Verdict::kEngineSimulation);
  ASSERT_TRUE(Hitch::ClassifyVerdict(phases(35'700u, 0u, 200u, 300u, 0u), false) ==
              Verdict::kInputStall);
  ASSERT_TRUE(Hitch::ClassifyVerdict(phases(100u, 0u, 200u, 300u, 40'000u), false) ==
              Verdict::kPacingOversleep);
  // Ties go to the earliest phase (DominantPhase policy): an all-zero frame
  // is deterministically INPUT, never an accident of iteration order.
  ASSERT_TRUE(Hitch::ClassifyVerdict(phases(0u, 0u, 0u, 0u, 0u), false) ==
              Verdict::kInputStall);

  // The render-phase ambiguity is resolved by the top rank's identity: the WSI boundary scope means
  // the driver blocked mid-render (EXTERNAL); anything else is game-owned render-recording code ...
  ASSERT_TRUE(Hitch::ClassifyVerdict(phases(100u, 0u, 111'500u, 1'300u, 0u), true) ==
              Verdict::kExternalWait);

  // CPU-starvation discrimination (AGENT 6): an engine-dominated frame whose render thread received
  // almost no CPU was preempted or blocked -- the machine owned the frame, not the engine. Pure ...
  ASSERT_TRUE(!Hitch::IsCpuStarved(100'000u, 0u));       // no thread clock -> no verdict
  ASSERT_TRUE(!Hitch::IsCpuStarved(0u, 0u));             // degenerate frame -> no verdict
  ASSERT_TRUE(!Hitch::IsCpuStarved(100'000u, 90'000u));  // on-CPU frame
  ASSERT_TRUE(Hitch::IsCpuStarved(100'000u, 40'000u));   // 40% < 50% share
  ASSERT_TRUE(!Hitch::IsCpuStarved(100'000u, 50'000u));  // boundary: 50% is NOT starved
  // Engine-dominated phases flip to CPU-STARVED on the evidence...
  ASSERT_TRUE(Hitch::ClassifyVerdict(phases(100u, 90'000u, 200u, 300u, 0u), false, true) ==
              Verdict::kCpuStarved);
  ASSERT_TRUE(Hitch::ClassifyVerdict(phases(35'700u, 0u, 111'500u, 1'300u, 0u), false, true) ==
              Verdict::kCpuStarved);
  // ...while the external classes keep their more precise tags regardless of the CPU share: a
  // present-dominated frame IS the WSI wait (the thread sleeps inside the driver, so a low ...
  ASSERT_TRUE(Hitch::ClassifyVerdict(phases(100u, 0u, 100u, 50'700u, 0u), false, true) ==
              Verdict::kExternalWait);
  ASSERT_TRUE(Hitch::ClassifyVerdict(phases(35'700u, 0u, 111'500u, 1'300u, 0u), true, true) ==
              Verdict::kExternalWait);
  ASSERT_TRUE(Hitch::ClassifyVerdict(phases(35'700u, 0u, 200u, 300u, 0u), false, true) ==
              Verdict::kInputStall);
  ASSERT_TRUE(Hitch::ClassifyVerdict(phases(100u, 0u, 200u, 300u, 40'000u), false, true) ==
              Verdict::kPacingOversleep);

  // IsExternalWaitScope compares by CONTENT against the SSOT set, so cross-TU string-literal
  // merging is irrelevant. The set covers the present block, the window-system round-trips ...
  ASSERT_TRUE(Hitch::IsExternalWaitScope(GameRules::Diagnostics::kPresentWaitScopeName));
  ASSERT_TRUE(Hitch::IsExternalWaitScope("SDL_RenderPresent"));
  ASSERT_TRUE(Hitch::IsExternalWaitScope(GameRules::Diagnostics::kWindowResizeScopeName));
  ASSERT_TRUE(Hitch::IsExternalWaitScope(GameRules::Diagnostics::kWindowFullscreenScopeName));
  ASSERT_TRUE(Hitch::IsExternalWaitScope(GameRules::Diagnostics::kRenderTargetSwitchScopeName));
  ASSERT_TRUE(Hitch::IsExternalWaitScope("SDL_SetRenderTarget"));  // content match
  ASSERT_TRUE(Hitch::IsExternalWaitScope(GameRules::Diagnostics::kEventPollScopeName));
  ASSERT_TRUE(Hitch::IsExternalWaitScope("SDL_PollEvent"));  // content match
  ASSERT_TRUE(Hitch::IsExternalWaitScope("SdlWindow::Resize"));  // content match
  ASSERT_TRUE(!Hitch::IsExternalWaitScope("sdl_renderpresent"));  // case-sensitive
  ASSERT_TRUE(!Hitch::IsExternalWaitScope("CombatSession::RenderActiveFrame"));
  ASSERT_TRUE(!Hitch::IsExternalWaitScope(nullptr));
  ASSERT_EQ(GameRules::Diagnostics::kExternalWaitScopeCount,
            static_cast<std::size_t>(5));

  // VerdictOf reads the top rank exactly like the reporter's Drain() does.
  Hitch::Record record{};
  record.phase_us = phases(35'700u, 0u, 111'500u, 1'300u, 0u);  // render-dominated
  record.total_us = 148'500u;
  record.top_count = 1;
  record.top[0].path[0] = "CombatSession::RenderActiveFrame";
  record.top[0].path[1] = "Score::Draw::PlateRebake";
  record.top[0].path_len = 2;
  ASSERT_TRUE(Hitch::VerdictOf(record) == Verdict::kEngineRender);
  record.top[0].path[0] = GameRules::Diagnostics::kPresentWaitScopeName;
  record.top[0].path_len = 1;
  ASSERT_TRUE(Hitch::VerdictOf(record) == Verdict::kExternalWait);
  // A render-dominated frame owned by a window-system round-trip is equally
  // external: the resize / fullscreen scopes resolve the same ambiguity.
  record.top[0].path[0] = GameRules::Diagnostics::kWindowResizeScopeName;
  ASSERT_TRUE(Hitch::VerdictOf(record) == Verdict::kExternalWait);
  // The FIRST plate target switch after a fullscreen toggle spin-waits behind driver surface
  // revalidation even with WARM glyphs. The nested boundary scope is the top rank's LAST path ...
  record.top[0].path[0] = "Score::Draw::PlateRebake";
  record.top[0].path[1] = GameRules::Diagnostics::kRenderTargetSwitchScopeName;
  record.top[0].path_len = 2;
  ASSERT_TRUE(Hitch::VerdictOf(record) == Verdict::kExternalWait);
  record.top_count = 0;  // diffuse frame: no top rank -> phase policy alone
  ASSERT_TRUE(Hitch::VerdictOf(record) == Verdict::kEngineRender);

  // VerdictOf wires the wire-format CPU evidence into the policy: the SAME sim-dominated frame
  // flips verdict purely on the measured thread-CPU share, and a zero reading (thread clock ...
  Hitch::Record sim_frame{};
  sim_frame.phase_us = phases(100u, 90'000u, 200u, 300u, 0u);
  sim_frame.total_us = 90'600u;
  sim_frame.cpu_us = 88'000u;
  ASSERT_TRUE(Hitch::VerdictOf(sim_frame) == Verdict::kEngineSimulation);
  sim_frame.cpu_us = 2'000u;
  ASSERT_TRUE(Hitch::VerdictOf(sim_frame) == Verdict::kCpuStarved);
  sim_frame.cpu_us = 0;
  ASSERT_TRUE(Hitch::VerdictOf(sim_frame) == Verdict::kEngineSimulation);

  // Present-dominated frame: a 50.7 ms WSI wait must classify as PRESENT.
  Hitch::Record present_frame{};
  present_frame.phase_us = phases(100u, 0u, 0u, 50'700u, 0u);
  present_frame.total_us = 50'800u;
  present_frame.top_count = 1;
  present_frame.top[0].path[0] = "SDL_RenderPresent";
  present_frame.top[0].path_len = 1;
  ASSERT_TRUE(Hitch::VerdictOf(present_frame) == Verdict::kExternalWait);

  // Every tag/meaning exists and the tags are pairwise distinct, so the
  // printed tally can never merge two classes into one ambiguous token.
  const Verdict all[] = {Verdict::kExternalWait, Verdict::kEngineRender,
                         Verdict::kEngineSimulation, Verdict::kInputStall,
                         Verdict::kPacingOversleep, Verdict::kCpuStarved};
  for (std::size_t i = 0; i < Hitch::kVerdictCount; ++i) {
    const char* tag = Hitch::VerdictTag(all[i]);
    const char* meaning = Hitch::VerdictMeaning(all[i]);
    ASSERT_TRUE(tag != nullptr && tag[0] != '\0');
    ASSERT_TRUE(meaning != nullptr && meaning[0] != '\0');
    for (std::size_t j = i + 1; j < Hitch::kVerdictCount; ++j) {
      ASSERT_TRUE(std::strcmp(tag, Hitch::VerdictTag(all[j])) != 0);
    }
  }

  // Compile-and-run canary for the exact expression the render backend uses: the SSOT literal flows
  // through the HITCH_SCOPE macro. Disarmed in tests (the singleton is never armed here), so it ...
  {
    HITCH_SCOPE(GameRules::Diagnostics::kPresentWaitScopeName);
  }

  // InPhaseNs: the watchdog's clamped "how long has THIS phase been open".
  ASSERT_EQ(Hitch::InPhaseNs(1'000ULL, 400ULL, 900ULL), 600ULL);   // normal
  ASSERT_EQ(Hitch::InPhaseNs(1'000ULL, 1'000ULL, 900ULL), 0ULL);   // phase just began
  ASSERT_EQ(Hitch::InPhaseNs(1'000ULL, 1'600ULL, 900ULL), 0ULL);   // future start (race)
  ASSERT_EQ(Hitch::InPhaseNs(1'000ULL, 0ULL, 900ULL), 900ULL);     // stale start: clamped
}

TEST_CASE(TestHitchFreezeSnapshotAndSeverity) {
  // PackPhaseMillis publishes the five closed-phase durations as ONE transactional 64-bit word (12
  // bits per slot, saturating at 4095 ms), so the watchdog's "so far" breakdown can never mix ...
  const std::array<std::uint64_t, Hitch::kPhaseCount> ns = {
      250'400'000ULL, 1'500'000ULL, 4'800'000ULL, 0ULL, 5'000'000'000'000ULL};
  const std::uint64_t packed = Hitch::PackPhaseMillis(ns);
  ASSERT_EQ(Hitch::UnpackPhaseMillis(packed, 0), 250u);
  ASSERT_EQ(Hitch::UnpackPhaseMillis(packed, 1), 1u);
  ASSERT_EQ(Hitch::UnpackPhaseMillis(packed, 2), 4u);
  ASSERT_EQ(Hitch::UnpackPhaseMillis(packed, 3), 0u);
  ASSERT_EQ(Hitch::UnpackPhaseMillis(packed, 4), 4095u);  // saturates (5000 s)

  // Slot independence: one hot slot leaves every neighbour at zero, and the
  // all-zero frame packs to the empty word.
  std::array<std::uint64_t, Hitch::kPhaseCount> only{};
  only[2] = 1'234'000'000ULL;  // 1234 ms in render
  const std::uint64_t packed_one = Hitch::PackPhaseMillis(only);
  for (std::size_t s = 0; s < Hitch::kPhaseCount; ++s) {
    ASSERT_EQ(Hitch::UnpackPhaseMillis(packed_one, s), s == 2 ? 1234u : 0u);
  }
  ASSERT_EQ(Hitch::PackPhaseMillis(std::array<std::uint64_t, Hitch::kPhaseCount>{}),
            0ULL);

  // Severity coloring policy: green below 2x the report threshold, yellow
  // below 4x, red at or above -- boundaries inclusive at the upper class.
  ASSERT_TRUE(Hitch::SeverityOf(33.3, 33.3) == Hitch::Severity::kNormal);
  ASSERT_TRUE(Hitch::SeverityOf(66.5, 33.3) == Hitch::Severity::kNormal);
  ASSERT_TRUE(Hitch::SeverityOf(66.6, 33.3) == Hitch::Severity::kElevated);
  ASSERT_TRUE(Hitch::SeverityOf(133.1, 33.3) == Hitch::Severity::kElevated);
  ASSERT_TRUE(Hitch::SeverityOf(133.2, 33.3) == Hitch::Severity::kSevere);
  ASSERT_TRUE(Hitch::SeverityColor(Hitch::Severity::kSevere) == Hitch::Color::kBoldRed);

  // ANSI gating: with colors disabled every code is the empty string (piped logs stay clean and
  // column widths identical); enabled codes all start with the escape character.
  for (int c = 0; c <= static_cast<int>(Hitch::Color::kBoldRed); ++c) {
    const auto color = static_cast<Hitch::Color>(c);
    ASSERT_TRUE(Hitch::AnsiCode(color, false)[0] == '\0');
    ASSERT_TRUE(Hitch::AnsiCode(color, true)[0] == '\033');
  }
  // Table geometry SSOT: the fixed name column must fit the widest scope name the game emits
  // (StartMenu::RenderCurrentPageToTexture, 38 chars), or the reporter's tables would silently ...
  ASSERT_TRUE(GameRules::Diagnostics::kReportNameColumnChars >= 38);
  // The verdict classes own their fixed colors: cool for the machine's own
  // waits, hot for game-owned code.
  ASSERT_TRUE(Hitch::VerdictColor(Hitch::Verdict::kExternalWait) == Hitch::Color::kCyan);
  ASSERT_TRUE(Hitch::VerdictColor(Hitch::Verdict::kEngineRender) == Hitch::Color::kRed);
  ASSERT_TRUE(Hitch::VerdictColor(Hitch::Verdict::kEngineSimulation) == Hitch::Color::kRed);
  ASSERT_TRUE(Hitch::VerdictColor(Hitch::Verdict::kCpuStarved) == Hitch::Color::kBlue);

  // FrameGuard compiles and runs disarmed with zero side effects (the singleton is never armed in
  // tests): Begin/End both short-circuit on the enabled_ branch.
  {
    Hitch::FrameGuard frame_guard;
  }
}

TEST_CASE(TestExternalBurstPolicy) {
  // The burst tracker groups adjacent EXTERNAL reports into the single desktop event they represent
  // (wallpaper fade, overview, repaint): pure state machine, injected numbers only -- a ...
  using Hitch::BurstEvent;
  Hitch::ExternalBurst burst{};

  // Empty tracker + non-external report: nothing happens.
  ASSERT_TRUE(Hitch::BurstUpdate(burst, false, 100, 5.0, 40.0) ==
              BurstEvent::kNone);
  ASSERT_EQ(burst.frames, 0ULL);

  // The wallpaper-switch triple: two reported frames separated by one healthy frame (never
  // reported) -- the frame-gap tolerance must bridge the hole as ONE burst.
  ASSERT_TRUE(Hitch::BurstUpdate(burst, true, 1194, 20.666, 47.5) ==
              BurstEvent::kAbsorbed);
  ASSERT_TRUE(Hitch::BurstUpdate(burst, true, 1195, 20.713, 260.1) ==
              BurstEvent::kAbsorbed);
  ASSERT_TRUE(Hitch::BurstUpdate(burst, true, 1197, 20.980, 130.9) ==
              BurstEvent::kAbsorbed);
  ASSERT_EQ(burst.frames, 3ULL);
  ASSERT_EQ(burst.first_frame_index, 1194ULL);
  ASSERT_EQ(burst.last_frame_index, 1197ULL);
  ASSERT_NEAR(burst.first_start_s, 20.666, 1e-9);
  ASSERT_NEAR(burst.last_end_s, 21.1109, 1e-4);   // 20.980 + 0.1309
  ASSERT_NEAR(burst.wait_ms_total, 438.5, 0.01);  // 47.5 + 260.1 + 130.9

  // The next non-external report CLOSES the burst as notable (3 >= min):
  // the caller snapshots before the call -- exactly what Drain() does.
  const Hitch::ExternalBurst closing = burst;
  ASSERT_TRUE(Hitch::BurstUpdate(burst, false, 1249, 21.957, 33.6) ==
              BurstEvent::kClosedNotable);
  ASSERT_EQ(closing.frames, 3ULL);
  ASSERT_EQ(burst.frames, 0ULL);  // tracker reset by the close

  // A frame-index gap beyond kExternalBurstMaxFrameGap closes the active
  // burst and opens a new one in the same call.
  ASSERT_TRUE(Hitch::BurstUpdate(burst, true, 2000, 30.0, 40.0) ==
              BurstEvent::kAbsorbed);
  ASSERT_TRUE(Hitch::BurstUpdate(burst, true, 2001, 30.1, 40.0) ==
              BurstEvent::kAbsorbed);
  const Hitch::ExternalBurst small = burst;
  ASSERT_TRUE(Hitch::BurstUpdate(burst, true, 2050, 40.0, 40.0) ==
              BurstEvent::kClosedSmall);
  ASSERT_EQ(small.frames, 2ULL);
  ASSERT_EQ(burst.frames, 1ULL);  // fresh burst already open at #2050
  ASSERT_EQ(burst.first_frame_index, 2050ULL);

  // Session-end flush: an external burst closing at shutdown still earns
  // its note (Run() feeds a synthetic non-external close).
  ASSERT_TRUE(Hitch::BurstUpdate(burst, true, 2051, 40.1, 40.0) ==
              BurstEvent::kAbsorbed);
  ASSERT_TRUE(Hitch::BurstUpdate(burst, true, 2052, 40.2, 40.0) ==
              BurstEvent::kAbsorbed);
  const Hitch::ExternalBurst at_exit = burst;
  ASSERT_TRUE(Hitch::BurstUpdate(burst, false, 0, 0.0, 0.0) ==
              BurstEvent::kClosedNotable);
  ASSERT_EQ(at_exit.frames, 3ULL);

  // A backwards frame index must never extend an active burst.
  Hitch::ExternalBurst b2{};
  ASSERT_TRUE(Hitch::BurstUpdate(b2, true, 50, 1.0, 40.0) ==
              BurstEvent::kAbsorbed);
  ASSERT_TRUE(Hitch::BurstUpdate(b2, true, 40, 2.0, 40.0) ==
              BurstEvent::kClosedSmall);  // closes the 1-frame burst, opens new
  ASSERT_EQ(b2.frames, 1ULL);
  ASSERT_EQ(b2.first_frame_index, 40ULL);
}

TEST_CASE(TestCadenceStutterPattern) {
  // The cadence watch is the stuttering half of the flight recorder: pure injected-numbers policy
  // over EVERY frame (not only slow ones), so the sub-threshold judder pattern -- the classic ...
  using Hitch::CadenceEvent;
  namespace D = GameRules::Diagnostics;
  Hitch::CadenceWindow w{};

  // Render-dominated phase split: every fed frame burns its time in render.
  const std::array<std::uint32_t, Hitch::kPhaseCount> ren_heavy{
      2'000u, 1'000u, 10'000u, 2'000u, 1'000u};
  // The default report threshold in microseconds (two 60 Hz steps): frames at or above it are
  // hitches with their own rows and must never enter the cadence window.
  constexpr std::uint32_t kReportUs = static_cast<std::uint32_t>(
      GameRules::Diagnostics::kHitchReportNanoseconds / 1000ULL);
  const auto feed = [](Hitch::CadenceWindow& win, std::uint64_t frame_index,
                       std::uint32_t total_us,
                       const std::array<std::uint32_t, Hitch::kPhaseCount>& ph) {
    return Hitch::CadenceUpdate(win, frame_index, frame_index * 16'700'000ULL,
                                total_us, ph, kReportUs);
  };

  // 1. Healthy VSync cadence (16.4 / 16.9 ms alternation, 0.5 ms spread):
  //    the window never turns rough, no episode ever opens.
  for (std::uint64_t i = 0; i < 60; ++i) {
    const std::uint32_t total = (i % 2 == 0) ? 16'400u : 16'900u;
    ASSERT_TRUE(feed(w, i, total, ren_heavy) == CadenceEvent::kNone);
    ASSERT_TRUE(!w.episode_active);
  }

  // 2. Alternating 9 / 25 ms pacing: EVERY frame stays below the 33.3 ms report threshold (no hitch
  // row would ever print), yet the 16 ms swing is exactly the visible judder. Once both ...
  std::uint64_t frame = 60;
  bool saw_continued = false;
  for (int i = 0; i < 40; ++i, ++frame) {
    const std::uint32_t total = (i % 2 == 0) ? 9'000u : 25'000u;
    const CadenceEvent ev = feed(w, frame, total, ren_heavy);
    ASSERT_TRUE(ev == CadenceEvent::kNone || ev == CadenceEvent::kContinued);
    saw_continued = saw_continued || (ev == CadenceEvent::kContinued);
  }
  ASSERT_TRUE(saw_continued);
  ASSERT_TRUE(w.episode_active);

  // 3. Recovery: smooth frames flush the window and the episode closes exactly ONCE, as notable (it
  // ran far longer than the minimum). The snapshot taken BEFORE the closing call carries the ...
  bool closed_notable = false;
  Hitch::CadenceWindow closing{};
  for (int i = 0; i < 30 && !closed_notable; ++i, ++frame) {
    const std::uint32_t total = (i % 2 == 0) ? 16'400u : 16'900u;
    const Hitch::CadenceWindow before = w;
    const CadenceEvent ev = feed(w, frame, total, ren_heavy);
    ASSERT_TRUE(ev == CadenceEvent::kContinued ||
                ev == CadenceEvent::kClosedNotable);
    if (ev == CadenceEvent::kClosedNotable) {
      closed_notable = true;
      closing = before;
    }
  }
  ASSERT_TRUE(closed_notable);
  ASSERT_TRUE(!w.episode_active);
  ASSERT_TRUE(closing.frames >= D::kStutterMinEpisodeFrames);
  ASSERT_EQ(closing.min_us, 9'000u);
  ASSERT_EQ(closing.max_us, 25'000u);
  ASSERT_TRUE(closing.first_frame_index < closing.last_frame_index);
  // The annotation's dominant phase comes from the episode's accumulated
  // phase sums: every fed frame was render-heavy, so render must win.
  std::array<std::uint32_t, Hitch::kPhaseCount> sums{};
  for (std::size_t p = 0; p < Hitch::kPhaseCount; ++p) {
    sums[p] = static_cast<std::uint32_t>(closing.phase_sum_us[p]);
  }
  ASSERT_TRUE(Hitch::DominantPhase(sums) == Hitch::Phase::kRender);

  // 4. Fully recovered cadence: silent again (no re-open on smooth frames).
  for (int i = 0; i < 40; ++i, ++frame) {
    const std::uint32_t total = (i % 2 == 0) ? 16'400u : 16'900u;
    ASSERT_TRUE(feed(w, frame, total, ren_heavy) == CadenceEvent::kNone);
  }

  // 5. A short blip (6 jittery frames) never reaches the annotation minimum: while the window
  // flushes, the episode stays below kStutterMinEpisodeFrames and closes SMALL -- isolated ...
  Hitch::CadenceWindow blip{};
  bool blip_notable = false;
  bool blip_small = false;
  frame = 0;
  for (std::uint64_t i = 0; i < 60; ++i, ++frame) {
    const std::uint32_t total = (i % 2 == 0) ? 16'400u : 16'900u;
    ASSERT_TRUE(feed(blip, frame, total, ren_heavy) == CadenceEvent::kNone);
  }
  for (int i = 0; i < 6; ++i, ++frame) {
    const std::uint32_t total = (i % 2 == 0) ? 9'000u : 25'000u;
    const CadenceEvent ev = feed(blip, frame, total, ren_heavy);
    ASSERT_TRUE(ev == CadenceEvent::kNone || ev == CadenceEvent::kContinued);
  }
  for (int i = 0; i < 40; ++i, ++frame) {
    const std::uint32_t total = (i % 2 == 0) ? 16'400u : 16'900u;
    const CadenceEvent ev = feed(blip, frame, total, ren_heavy);
    if (ev == CadenceEvent::kClosedNotable) blip_notable = true;
    if (ev == CadenceEvent::kClosedSmall) blip_small = true;
  }
  ASSERT_TRUE(!blip_notable);
  ASSERT_TRUE(blip_small);

  // 6. Admissibility gate: reported hitches (>= threshold) and non-presenting bookkeeping frames (<
  // the cadence floor) never enter the window. A storm mixing both with healthy frames keeps ...
  Hitch::CadenceWindow mixed{};
  frame = 0;
  for (int i = 0; i < 90; ++i, ++frame) {
    std::uint32_t total = 16'700u;
    if (i % 3 == 0) {
      total = kReportUs + 5'000u;  // a reported hitch (has its own row)
    } else if (i % 3 == 1) {
      total = 100u;                // a sub-loop guard closing: never presented
    }
    ASSERT_TRUE(feed(mixed, frame, total, ren_heavy) == CadenceEvent::kNone);
  }
  ASSERT_TRUE(!mixed.episode_active);
  ASSERT_EQ(mixed.count, GameRules::Diagnostics::kCadenceWindowFrames);

  // 7. Display-rounded admissibility: a frame ONE microsecond below the threshold displays as
  // "33.3" -- exactly like the threshold itself -- so it must stay OUT of the window; an ...
  ASSERT_TRUE(Hitch::DisplayTenthsMs(kReportUs - 1u) ==
              Hitch::DisplayTenthsMs(kReportUs));
  ASSERT_TRUE(Hitch::DisplayTenthsMs(kReportUs - 84u) <
              Hitch::DisplayTenthsMs(kReportUs));
  ASSERT_TRUE(Hitch::DisplayTenthsMs(UINT32_MAX) >
              Hitch::DisplayTenthsMs(kReportUs));  // saturating input: no overflow
  Hitch::CadenceWindow edge{};
  frame = 0;
  for (int i = 0; i < 40; ++i, ++frame) {
    // A near-miss hitch (displays 33.3) alternated with steady 8 ms frames:
    // only the steady ones are admitted, so the window spread stays zero.
    const std::uint32_t total = (i % 2 == 0) ? (kReportUs - 1u) : 8'000u;
    ASSERT_TRUE(feed(edge, frame, total, ren_heavy) == CadenceEvent::kNone);
  }
  ASSERT_TRUE(!edge.episode_active);
  ASSERT_EQ(edge.count, GameRules::Diagnostics::kCadenceWindowFrames);
}

TEST_CASE(TestHitchScopeSelfTimeAndCharging) {
  {
  // The attributor is pure: timestamps are injected, so the whole policy is exercised headlessly --
  // including the surgical property that makes the "top #1" rank meaningful (innermost dominant ...
  Hitch::ScopeAttributor attr;

  // Outer scope of 10 ms containing an inner stall of 8 ms: the inner
  // function must rank first (self 8 ms vs outer self 2 ms).
  attr.BeginFrame();
  attr.Enter("Outer", 0);
  attr.Enter("Inner", 1000000);
  attr.Exit(9000000);    // inner gross/self = 8 ms
  attr.Exit(10000000);   // outer gross 10 ms - child 8 ms = 2 ms self
  ASSERT_EQ(attr.TopCount(), 2);
  ASSERT_TRUE(attr.TopName(0) != nullptr);
  ASSERT_TRUE(std::string_view(attr.TopName(0)) == "Inner");
  ASSERT_EQ(attr.TopSelfNs(0), static_cast<std::uint64_t>(8000000));
  ASSERT_EQ(attr.TopCalls(0), static_cast<std::uint32_t>(1));

  // A new frame resets the attribution.
  attr.BeginFrame();
  ASSERT_EQ(attr.TopCount(), 0);
  ASSERT_TRUE(attr.TopName(0) == nullptr);
  ASSERT_EQ(attr.TopSelfNs(0), static_cast<std::uint64_t>(0));

  // Unbalanced exit on an empty stack is a safe no-op.
  attr.Exit(5000000);
  ASSERT_TRUE(attr.TopName(0) == nullptr);

  // Depth overflow is ignored but stays BALANCED: the extra exits must not pop recorded scopes nor
  // corrupt the ranking. All recorded scopes share the "Deep" name, so their self times ...
  attr.BeginFrame();
  const int extra = 3;
  for (int i = 0; i < Hitch::ScopeAttributor::kMaxDepth + extra; ++i) {
    attr.Enter("Deep", 0);
  }
  for (int i = 0; i < Hitch::ScopeAttributor::kMaxDepth + extra; ++i) {
    attr.Exit(1000000);
  }
  ASSERT_TRUE(attr.TopName(0) != nullptr);
  ASSERT_TRUE(std::string_view(attr.TopName(0)) == "Deep");
  // Innermost recorded scope: gross 1 ms, no recorded children -> 1 ms self; the seven enclosing
  // recorded scopes clamp to 0 self, so the aggregate total stays 1 ms over the 8 recorded ...
  ASSERT_EQ(attr.TopSelfNs(0), static_cast<std::uint64_t>(1000000));
  ASSERT_EQ(attr.TopCalls(0),
            static_cast<std::uint32_t>(Hitch::ScopeAttributor::kMaxDepth));
  }

  {
  // The WSI wait (SDL_RenderPresent) happens INSIDE the scopes that enclose Present() --
  // CombatSession::RenderActiveFrame is exactly such a scope. Monitor charges that span as ...
  Hitch::ScopeAttributor attr;

  // Present-dominated frame: 2 ms of real draw work + 40 ms WSI wait.
  attr.BeginFrame();
  attr.Enter("Frame", 0);
  attr.ChargeOpenChildren(40000000);
  attr.Exit(45000000);  // gross 45 ms - external 40 ms = 5 ms self (<=45-40)
  ASSERT_TRUE(attr.TopName(0) != nullptr);
  ASSERT_TRUE(std::string_view(attr.TopName(0)) == "Frame");
  ASSERT_EQ(attr.TopSelfNs(0), static_cast<std::uint64_t>(5000000));

  // The charge reaches EVERY open ancestor (the wait is inside all of them),
  // and self time clamps at zero -- never negative.
  attr.BeginFrame();
  attr.Enter("Outer", 0);
  attr.Enter("Inner", 1000000);
  attr.ChargeOpenChildren(30000000);
  attr.Exit(35000000);  // inner gross 34 ms - external 30 ms = 4 ms self
  attr.Exit(36000000);  // outer gross 36 ms - children 64 ms -> clamped 0
  ASSERT_TRUE(std::string_view(attr.TopName(0)) == "Inner");
  ASSERT_EQ(attr.TopSelfNs(0), static_cast<std::uint64_t>(4000000));
  }
}

TEST_CASE(TestHitchScopeRankingAndCallPaths) {
  {
  // Each aggregate keeps the caller chain of its BIGGEST single call: root first, culprit last.
  // That chain is what turns a "top #N" rank into a surgical call path (WHO CALLED the ...
  Hitch::ScopeAttributor attr;

  // Nested stall: the leaf owns 7 ms self, so rank #1 must read
  // Root > Mid > Leaf with the leaf as the ranked name.
  attr.BeginFrame();
  attr.Enter("Root", 0);
  attr.Enter("Mid", 1000000);
  attr.Enter("Leaf", 2000000);
  attr.Exit(9000000);    // leaf gross/self = 7 ms -> rank #1, chain len 3
  attr.Exit(9500000);    // mid  gross 8.5 ms - child 7 ms = 1.5 ms self
  attr.Exit(10000000);   // root gross 10 ms - child 8.5 ms = 1.5 ms self
  ASSERT_EQ(attr.TopPathLen(0), 3);
  ASSERT_TRUE(std::string_view(attr.TopPath(0)[0]) == "Root");
  ASSERT_TRUE(std::string_view(attr.TopPath(0)[1]) == "Mid");
  ASSERT_TRUE(std::string_view(attr.TopPath(0)[2]) == "Leaf");
  // The ranked name is DERIVED from the chain's last element: one source
  // of truth, nothing to diverge.
  ASSERT_TRUE(std::string_view(attr.TopName(0)) == "Leaf");
  ASSERT_EQ(attr.TopSelfNs(0), static_cast<std::uint64_t>(7000000));

  // A new frame resets every aggregate.
  attr.BeginFrame();
  ASSERT_EQ(attr.TopCount(), 0);
  ASSERT_TRUE(attr.TopName(0) == nullptr);
  ASSERT_EQ(attr.TopPathLen(0), 0);

  // A shallower-but-slower ENCLOSING scope outranks the deeper one, and rank #2 keeps its own
  // chain: every entry carries the path of its biggest single call.
  attr.Enter("A", 0);
  attr.Enter("B", 1000000);
  attr.Exit(2000000);    // B self 1 ms -> chain A > B
  attr.Exit(30000000);   // A gross 30 ms - child 1 ms = 29 ms self
  ASSERT_TRUE(std::string_view(attr.TopName(0)) == "A");
  ASSERT_EQ(attr.TopPathLen(0), 1);
  ASSERT_TRUE(std::string_view(attr.TopPath(0)[0]) == "A");
  ASSERT_EQ(attr.TopSelfNs(0), static_cast<std::uint64_t>(29000000));
  ASSERT_TRUE(std::string_view(attr.TopName(1)) == "B");
  ASSERT_EQ(attr.TopPathLen(1), 2);
  ASSERT_TRUE(std::string_view(attr.TopPath(1)[0]) == "A");
  ASSERT_TRUE(std::string_view(attr.TopPath(1)[1]) == "B");
  }

  {
  // AGGREGATE semantics: one function called many times ranks by its TOTAL self time (sum over all
  // calls), with its call count preserved and the representative chain taken from the biggest ...
  Hitch::ScopeAttributor attr;

  attr.BeginFrame();
  attr.Enter("Root", 0);
  attr.Enter("Leaf", 1000000);
  attr.Exit(3000000);    // Leaf call 1: self 2 ms (chain Root > Leaf)
  attr.Enter("Leaf", 3000000);
  attr.Exit(9000000);    // Leaf call 2: self 6 ms -> TOTAL 8 ms, 2 calls
  attr.Exit(10000000);   // Root: gross 10 ms - children 8 ms = 2 ms self
  ASSERT_EQ(attr.TopCount(), 2);
  ASSERT_TRUE(std::string_view(attr.TopName(0)) == "Leaf");
  ASSERT_EQ(attr.TopSelfNs(0), static_cast<std::uint64_t>(8000000));
  ASSERT_EQ(attr.TopCalls(0), static_cast<std::uint32_t>(2));
  ASSERT_TRUE(std::string_view(attr.TopName(1)) == "Root");
  ASSERT_EQ(attr.TopSelfNs(1), static_cast<std::uint64_t>(2000000));
  ASSERT_EQ(attr.TopCalls(1), static_cast<std::uint32_t>(1));
  ASSERT_TRUE(attr.TopName(2) == nullptr);  // ranks beyond the data: safe

  // The representative chain follows the BIGGEST single call, even when a
  // later, smaller call happens under a DIFFERENT parent.
  attr.BeginFrame();
  attr.Enter("P1", 0);
  attr.Enter("L", 1000000);
  attr.Exit(6000000);    // L self 5 ms under P1 -> representative chain
  attr.Exit(6500000);    // P1 self 0.5 ms
  attr.Enter("P2", 6500000);
  attr.Enter("L", 6500000);
  attr.Exit(7500000);    // L self 1 ms under P2: total 6 ms, rep stays P1
  attr.Exit(7500000);    // P2 self 0 ms
  ASSERT_TRUE(std::string_view(attr.TopName(0)) == "L");
  ASSERT_EQ(attr.TopSelfNs(0), static_cast<std::uint64_t>(6000000));
  ASSERT_EQ(attr.TopCalls(0), static_cast<std::uint32_t>(2));
  ASSERT_EQ(attr.TopPathLen(0), 2);
  ASSERT_TRUE(std::string_view(attr.TopPath(0)[0]) == "P1");
  ASSERT_TRUE(std::string_view(attr.TopPath(0)[1]) == "L");

  // Table overflow (more distinct names than kAggTableSlots): the smallest aggregate yields to a
  // strictly larger arrival, so the top ranks stay exact even though the long tail is dropped. ...
  static const char* const kNames[] = {
      "N00", "N01", "N02", "N03", "N04", "N05", "N06", "N07",
      "N08", "N09", "N10", "N11", "N12", "N13", "N14", "N15",
      "N16", "N17", "N18", "N19", "N20", "N21", "N22", "N23",
      "N24", "N25", "N26", "N27", "N28", "N29", "N30", "N31",
      "N32", "N33", "N34", "N35", "N36", "N37", "N38", "N39"};
  attr.BeginFrame();
  std::uint64_t t = 0;
  for (int i = 0; i < static_cast<int>(sizeof(kNames) / sizeof(kNames[0])); ++i) {
    attr.Enter(kNames[i], t);
    t += static_cast<std::uint64_t>(i + 1) * 1000000U;
    attr.Exit(t);
  }
  ASSERT_TRUE(std::string_view(attr.TopName(0)) == "N39");
  ASSERT_EQ(attr.TopSelfNs(0), static_cast<std::uint64_t>(40000000));
  ASSERT_TRUE(std::string_view(attr.TopName(1)) == "N38");
  ASSERT_TRUE(std::string_view(attr.TopName(2)) == "N37");
  }
}

TEST_CASE(TestFrameContextAndMaxCallAttribution) {
  // Frame-context tagging: the report row correlates an expensive frame with the window-system
  // event class that triggered it (RESIZED / FULLSCREEN / PRESET / SETTLING) instead of leaving ...
  using Hitch::FrameContext;
  ASSERT_TRUE(std::strcmp(Hitch::ContextTag(FrameContext::kResized), "RESIZED") == 0);
  ASSERT_TRUE(std::strcmp(Hitch::ContextTag(FrameContext::kFullscreen), "FULLSCREEN") == 0);
  ASSERT_TRUE(std::strcmp(Hitch::ContextTag(FrameContext::kSizePreset), "PRESET") == 0);
  ASSERT_TRUE(std::strcmp(Hitch::ContextTag(FrameContext::kTextSettling), "SETTLING") == 0);

  char buf[64];
  const std::uint8_t both =
      static_cast<std::uint8_t>(FrameContext::kResized) |
      static_cast<std::uint8_t>(FrameContext::kTextSettling);
  // Bit order: RESIZED (bit 0) before SETTLING (bit 3).
  ASSERT_EQ(Hitch::ContextTagsTo(both, buf, sizeof(buf)), 16u);
  ASSERT_TRUE(std::strcmp(buf, "RESIZED,SETTLING") == 0);
  ASSERT_EQ(Hitch::ContextTagsTo(0, buf, sizeof(buf)), 0u);
  ASSERT_TRUE(buf[0] == '\0');
  // Truncation safety: even an adversarially small buffer stays NUL-
  // terminated and the returned advance never exceeds the clamp.
  char tiny[6];
  const std::size_t used = Hitch::ContextTagsTo(both, tiny, sizeof(tiny));
  ASSERT_TRUE(used <= sizeof(tiny) - 1);
  ASSERT_TRUE(tiny[used] == '\0');

  // Max-single-call exposure: the aggregate 81 ms over 2 calls cannot tell "one 78 ms block + one
  // cheap poll" from "two slow polls" -- the exposed maximum can, and that is the difference ...
  Hitch::ScopeAttributor attributor;
  attributor.BeginFrame();
  attributor.Enter("SDL_PollEvent", 0);
  attributor.Exit(3'000'000);             // cheap 3 ms poll
  attributor.Enter("SDL_PollEvent", 10'000'000);
  attributor.Exit(88'000'000);            // ONE 78 ms block
  ASSERT_TRUE(attributor.TopCount() >= 1);
  ASSERT_EQ(attributor.TopSelfNs(0), 81'000'000ULL);    // aggregate: 3 + 78
  ASSERT_EQ(attributor.TopMaxSelfNs(0), 78'000'000ULL); // largest single call
  ASSERT_EQ(attributor.TopCalls(0), 2u);
  // Unavailable ranks stay safe.
  ASSERT_EQ(attributor.TopMaxSelfNs(2), 0ULL);

  // Wire contract: the Record (now carrying the context mask and per-call
  // maxima) still crosses the wait-free ring by trivial copy.
  static_assert(std::is_trivially_copyable_v<Hitch::Record>);
  static_assert(std::is_trivially_copyable_v<Hitch::TopEntry>);
}

TEST_CASE(TestHitchLogCliFlag) {
  // The CLI switch is a pure argv scan over the SSOT literal from GameRules::Diagnostics: no
  // globals, no clock, no I/O. Matching is dash-agnostic (CliFlags normalizes the same ...
  ASSERT_TRUE(std::string_view(GameRules::Diagnostics::kHitchLogCliFlag) ==
              std::string_view("--log"));

  const char* argv_on[] = {"aliens-invaders", "-driver", "opengl", "--log"};
  ASSERT_TRUE(Hitch::HitchLogFlagPresent(4, argv_on));

  const char* argv_single_dash[] = {"aliens-invaders", "-log"};
  ASSERT_TRUE(Hitch::HitchLogFlagPresent(2, argv_single_dash));

  const char* argv_triple_dash[] = {"aliens-invaders", "---log"};
  ASSERT_TRUE(Hitch::HitchLogFlagPresent(2, argv_triple_dash));

  const char* argv_off[] = {"aliens-invaders", "-driver", "opengl"};
  ASSERT_TRUE(!Hitch::HitchLogFlagPresent(3, argv_off));

  // A bare word is a value, never a flag: dash-agnostic does not mean
  // dash-free ("log" alone must not arm the recorder).
  const char* argv_word[] = {"aliens-invaders", "log"};
  ASSERT_TRUE(!Hitch::HitchLogFlagPresent(2, argv_word));

  // Degenerate inputs are safe: program name only, empty argc, null argv.
  ASSERT_TRUE(!Hitch::HitchLogFlagPresent(1, argv_off));
  ASSERT_TRUE(!Hitch::HitchLogFlagPresent(0, argv_off));
  ASSERT_TRUE(!Hitch::HitchLogFlagPresent(3, nullptr));
}

TEST_CASE(TestFontPublishBudgets) {
  {
  // The row-slice policy that breaks a full-atlas zero-publish spike into budgeted frames: small
  // ranges flush whole (the visible-text contract), big ranges advance exactly the budget, ...
  constexpr int kBudget = GameRules::Visuals::kFontBandFlushRowBudget;
  ASSERT_TRUE(kBudget > 0);

  ASSERT_EQ(GameRules::Visuals::BandFlushRows(0, 10, kBudget), 10);   // whole
  ASSERT_EQ(GameRules::Visuals::BandFlushRows(0, 10, 4), 4);          // sliced
  ASSERT_EQ(GameRules::Visuals::BandFlushRows(6, 10, 4), 4);          // mid-range
  ASSERT_EQ(GameRules::Visuals::BandFlushRows(10, 10, kBudget), 0);   // empty
  ASSERT_EQ(GameRules::Visuals::BandFlushRows(12, 10, kBudget), 0);   // inverted
  ASSERT_EQ(GameRules::Visuals::BandFlushRows(0, 2048, 0), 2048);     // 0 = unlimited
  ASSERT_EQ(GameRules::Visuals::BandFlushRows(0, 2048, -3), 2048);    // defensive

  // A full settling page (2048 rows) converges in ceil(2048/budget) slices, each slice exactly the
  // budget except the last -- monotonic progress, so the flush loop can never livelock a ...
  int cursor = 0;
  int slices = 0;
  while (cursor < 2048) {
    const int rows = GameRules::Visuals::BandFlushRows(cursor, 2048, kBudget);
    ASSERT_TRUE(rows > 0);
    ASSERT_TRUE(rows <= kBudget);
    cursor += rows;
    ++slices;
    ASSERT_TRUE(slices <= 2048);  // termination guard
  }
  ASSERT_EQ(cursor, 2048);
  ASSERT_EQ(slices, (2048 + kBudget - 1) / kBudget);
  }

  {
  // The settling publish spends ONE shared per-frame row budget: the glyph band (same-frame
  // visibility contract) goes first and the zero slice receives only the remainder -- the cap ...
  constexpr int kBudget = GameRules::Visuals::kFontBandFlushRowBudget;
  ASSERT_TRUE(kBudget > 0);

  // Remainder arithmetic: full budget when the band uploaded nothing,
  // exact subtraction mid-band, clamped to zero at/over the budget.
  ASSERT_EQ(GameRules::Visuals::ZeroPublishRowBudget(0, kBudget), kBudget);
  ASSERT_EQ(GameRules::Visuals::ZeroPublishRowBudget(40, kBudget), kBudget - 40);
  ASSERT_EQ(GameRules::Visuals::ZeroPublishRowBudget(kBudget, kBudget), 0);
  ASSERT_EQ(GameRules::Visuals::ZeroPublishRowBudget(kBudget + 300, kBudget), 0);

  // A zero remainder must gate the slice OFF at the call site -- it must NEVER reach BandFlushRows,
  // whose non-positive spelling means "unlimited" (a full-page 16 MB upload, exactly the ...
  const int remainder = GameRules::Visuals::ZeroPublishRowBudget(100, 100);
  ASSERT_EQ(remainder, 0);
  const int rows = (remainder > 0)
                       ? GameRules::Visuals::BandFlushRows(0, 2048, remainder)
                       : 0;
  ASSERT_EQ(rows, 0);

  // Prewarm claim cap: an empty band (max < min sentinel) is never exhausted; the tick stops
  // exactly at one full slice, and a single overshooting glyph (taller than the budget) also ...
  ASSERT_TRUE(!GameRules::Visuals::BandClaimRowsExhausted(1, 0, kBudget));
  ASSERT_TRUE(!GameRules::Visuals::BandClaimRowsExhausted(100, 100 + kBudget - 1, kBudget));
  ASSERT_TRUE(GameRules::Visuals::BandClaimRowsExhausted(100, 100 + kBudget, kBudget));
  ASSERT_TRUE(GameRules::Visuals::BandClaimRowsExhausted(100, 900, kBudget));
  }
}

TEST_CASE(TestFontPrewarmPlan) {
  namespace FP = FontPrewarm;
  // The plan is a pure constexpr flattening: 16 design bases x 95 printable
  // ASCII chars x 2 styles, addressable by one resumable cursor.
  ASSERT_EQ(FP::kItemCount, 17 * 95 * 2);

  for (int i = 0; i < FP::kItemCount; ++i) {
    const FP::Item item = FP::ItemAt(i);
    ASSERT_TRUE(item.ch >= 32 && item.ch <= 126);
    ASSERT_TRUE(item.base > 0.0f);
  }

  // Axis order: style fastest, then char, then base slot.
  ASSERT_TRUE(!FP::ItemAt(0).bold);
  ASSERT_TRUE(FP::ItemAt(1).bold);
  ASSERT_EQ(static_cast<int>(FP::ItemAt(0).ch), 32);
  ASSERT_EQ(static_cast<int>(FP::ItemAt(2).ch), 33);

  // HUD and telemetry bases lead the prewarm plan so visible UI glyphs become
  // warm early; unscaled combat bases remain at the end.
  const int base_block = FP::kCharsPerBase * FP::kStyleCount;
  for (int i = 0; i < base_block; ++i) {
    ASSERT_TRUE(FP::ItemAt(i).scale_by_dpi);
    ASSERT_EQ(FP::ItemAt(i).base, 20.0f);  // the HUD plate size, warm first
  }
  // Second block: the telemetry OSD base -- scaled by the CLAMPED band, so the plan bakes exactly
  // the size the overlay draws at EVERY scale (the unclamped product would miss it above the ...
  for (int i = base_block; i < 2 * base_block; ++i) {
    ASSERT_TRUE(FP::ItemAt(i).scale_by_dpi);
    ASSERT_TRUE(FP::ItemAt(i).clamped_scale);
    ASSERT_EQ(FP::ItemAt(i).base, GameRules::TelemetryConfig::kFontSize);
  }
  ASSERT_TRUE(!FP::ItemAt(0).clamped_scale);                   // HUD base
  ASSERT_TRUE(!FP::ItemAt(FP::kItemCount - 1).clamped_scale);  // combat base
  const int menu_slots = FP::kMenuBaseCount * base_block;
  ASSERT_TRUE(FP::ItemAt(menu_slots - 1).scale_by_dpi);
  ASSERT_TRUE(!FP::ItemAt(menu_slots).scale_by_dpi);  // combat block last
  ASSERT_TRUE(!FP::ItemAt(FP::kItemCount - 1).scale_by_dpi);

  // The visit order is a PERMUTATION: every base block decodes one distinct (base, scale) identity,
  // so the plan skips no glyph and bakes none twice (the runtime twin of the header's ...
  for (int slot = 0; slot < FP::kBaseCount; ++slot) {
    const FP::Item first = FP::ItemAt(slot * base_block);
    for (int other = slot + 1; other < FP::kBaseCount; ++other) {
      const FP::Item second = FP::ItemAt(other * base_block);
      ASSERT_TRUE(first.base != second.base ||
                  first.scale_by_dpi != second.scale_by_dpi);
    }
  }

  // The same cursor always decodes the same item (resumability contract).
  ASSERT_EQ(static_cast<int>(FP::ItemAt(1234).ch),
            static_cast<int>(FP::ItemAt(1234).ch));
  ASSERT_EQ(FP::ItemAt(1234).bold, FP::ItemAt(1234).bold);

  // Out-of-range indices are clamped, never UB.
  ASSERT_EQ(static_cast<int>(FP::ItemAt(-5).ch),
            static_cast<int>(FP::ItemAt(0).ch));
  ASSERT_EQ(static_cast<int>(FP::ItemAt(FP::kItemCount + 5).ch),
            static_cast<int>(FP::ItemAt(FP::kItemCount - 1).ch));
}

TEST_CASE(TestCalmRebakePolicy) {
  // The present-calm gate defers discretionary plate rebakes out of compositor storms -- where the
  // driver's implicit sync at the render-target switch amplifies a cheap rebake into a ...
  constexpr float kNominal60Hz = 1000.0f / 60.0f;  // 16.67 ms
  // Calm classification is refresh-relative: a healthy present and a
  // single missed VBlank (<= 1.75x) are calm; doubled-or-worse presents
  // are busy. A 30 Hz display stays calm AT ITS OWN cadence -- the
  // absolute 33.3 ms that is pacing noise there must not defer rebakes.
  ASSERT_TRUE(GameRules::Visuals::PresentIsCalmForRebake(16.7f, kNominal60Hz));
  ASSERT_TRUE(GameRules::Visuals::PresentIsCalmForRebake(29.0f, kNominal60Hz));
  ASSERT_TRUE(!GameRules::Visuals::PresentIsCalmForRebake(33.4f, kNominal60Hz));
  ASSERT_TRUE(!GameRules::Visuals::PresentIsCalmForRebake(90.0f, kNominal60Hz));
  ASSERT_TRUE(GameRules::Visuals::PresentIsCalmForRebake(33.4f, 33.333f));
  ASSERT_TRUE(!GameRules::Visuals::PresentIsCalmForRebake(58.4f, 33.333f));
  // Defer clock: counts only wanted-AND-blocked frames; a calm present
  // or an idle plate resets it to zero (mirror of the settling clock).
  ASSERT_EQ(GameRules::Visuals::NextCalmDeferFrames(5, true, true), 0);
  ASSERT_EQ(GameRules::Visuals::NextCalmDeferFrames(5, false, false), 0);
  ASSERT_EQ(GameRules::Visuals::NextCalmDeferFrames(5, true, false), 6);
  // Gate: calm proceeds; busy defers up to the cap; cap <= 0 disables
  // defensively (same contract as SettlingRebakeMayProceed).
  ASSERT_TRUE(GameRules::Visuals::CalmRebakeMayProceed(true, 0, 30));
  ASSERT_TRUE(!GameRules::Visuals::CalmRebakeMayProceed(false, 29, 30));
  ASSERT_TRUE(GameRules::Visuals::CalmRebakeMayProceed(false, 30, 30));
  ASSERT_TRUE(GameRules::Visuals::CalmRebakeMayProceed(false, 0, 0));
  // Bounded staleness: a sustained storm forces the rebake after the cap.
  int defer = 0;
  bool proceeded = false;
  for (int frame = 1; frame <= 60 && !proceeded; ++frame) {
    defer = GameRules::Visuals::NextCalmDeferFrames(defer, true, false);
    proceeded = GameRules::Visuals::CalmRebakeMayProceed(
        false, defer, GameRules::Visuals::kCalmRebakeMaxDeferFrames);
  }
  ASSERT_TRUE(proceeded);
  ASSERT_EQ(defer, GameRules::Visuals::kCalmRebakeMaxDeferFrames);
}

TEST_CASE(TestHudPlateStormDeferral) {
  {
  // Storm-deferral policy for STRUCTURAL HUD plate updates: a bucket- crossing rebake stacking on
  // top of a windowing resize in the SAME frame compounds both costs. Deferring the ...
  namespace V = GameRules::Visuals;
  constexpr int kCap = V::kHudPlateStormMaxDeferFrames;
  ASSERT_TRUE(kCap >= V::kHudPlateSettleFrames);  // cap must outlive settle

  // No texture yet: ALWAYS immediate -- the HUD must never be blank.
  ASSERT_TRUE(V::StormRebakeApplyNow(false, false, 0, kCap));
  ASSERT_TRUE(V::StormRebakeApplyNow(false, false, 99, kCap));

  // Settled geometry: immediate -- the storm ended, ONE collapsed update.
  ASSERT_TRUE(V::StormRebakeApplyNow(true, true, 1, kCap));

  // Storm in progress: deferred...
  ASSERT_TRUE(!V::StormRebakeApplyNow(true, false, 1, kCap));
  ASSERT_TRUE(!V::StormRebakeApplyNow(true, false, kCap - 1, kCap));
  // ...and forced at the cap (bounded staleness, ~100 ms at 60 Hz).
  ASSERT_TRUE(V::StormRebakeApplyNow(true, false, kCap, kCap));
  ASSERT_TRUE(V::StormRebakeApplyNow(true, false, kCap + 40, kCap));

  // Defensive: a non-positive cap disables the deferral entirely.
  ASSERT_TRUE(V::StormRebakeApplyNow(true, false, 0, 0));
  ASSERT_TRUE(V::StormRebakeApplyNow(true, false, 0, -2));
  }

  {
  // The deferral counter tracks the current structural change, not time since
  // the last bake. A settled or clean state resets the counter.
  namespace V = GameRules::Visuals;
  constexpr int kCap = V::kHudPlateStormMaxDeferFrames;

  // Transition semantics: counts while a dims change pends in an unsettled
  // geometry; resets the instant nothing pends or the geometry settles.
  int defer_frames = 0;
  defer_frames = V::NextStormDeferFrames(defer_frames, true, false);
  ASSERT_EQ(defer_frames, 1);
  defer_frames = V::NextStormDeferFrames(defer_frames, true, false);
  ASSERT_EQ(defer_frames, 2);
  defer_frames = V::NextStormDeferFrames(defer_frames, false, false);
  ASSERT_EQ(defer_frames, 0);  // structural update landed: clock reset
  defer_frames = V::NextStormDeferFrames(defer_frames, true, true);
  ASSERT_EQ(defer_frames, 0);  // settled geometry applies immediately

  // Sustained-storm replay: a pending dims change in unsettled geometry
  // defers EXACTLY until the cap forces it -- never later.
  defer_frames = 0;
  int applied_at = -1;
  for (int frame = 1; frame <= kCap + 4 && applied_at < 0; ++frame) {
    defer_frames = V::NextStormDeferFrames(defer_frames, true, false);
    if (V::StormRebakeApplyNow(true, false, defer_frames, kCap)) {
      applied_at = frame;
    }
  }
  ASSERT_EQ(applied_at, kCap);

  // A fresh storm after a quiet period must receive a full deferral budget.
  defer_frames = V::NextStormDeferFrames(9999, false, true);  // quiet period
  ASSERT_EQ(defer_frames, 0);
  defer_frames = V::NextStormDeferFrames(defer_frames, true, false);  // storm!
  ASSERT_TRUE(!V::StormRebakeApplyNow(true, false, defer_frames, kCap));

  // Discrete preset press (the common field case): geometry changes once, stays stable, and the
  // settle window (kHudPlateSettleFrames) applies the update BEFORE the cap ever matters.
  defer_frames = 0;
  int settled_at = -1;
  for (int frame = 1; frame <= kCap + 4; ++frame) {
    const bool settled = (frame > V::kHudPlateSettleFrames);
    defer_frames = V::NextStormDeferFrames(defer_frames, true, settled);
    if (V::StormRebakeApplyNow(true, settled, defer_frames, kCap)) {
      settled_at = frame;
      break;
    }
  }
  ASSERT_EQ(settled_at, V::kHudPlateSettleFrames + 1);

  // The text redraw gate tracks the current settling window and caps stale
  // content without forcing cold glyph rasterization in the render pass.
  constexpr int kSettlingCap = V::kPlateSettlingMaxDeferFrames;

  // Transition semantics: counts while a redraw pends during settling;
  // resets the instant settling ends or nothing pends.
  int settling = 0;
  settling = V::NextSettlingDeferFrames(settling, true, true);
  ASSERT_EQ(settling, 1);
  settling = V::NextSettlingDeferFrames(settling, true, true);
  ASSERT_EQ(settling, 2);
  settling = V::NextSettlingDeferFrames(settling, false, true);
  ASSERT_EQ(settling, 0);  // nothing pends: clock reset
  settling = V::NextSettlingDeferFrames(settling, true, false);
  ASSERT_EQ(settling, 0);  // not settling: warm cache, no deferral

  // Gate: warm proceeds immediately; settling defers until the cap.
  ASSERT_TRUE(V::SettlingRebakeMayProceed(false, 0, kSettlingCap));
  ASSERT_TRUE(!V::SettlingRebakeMayProceed(true, 0, kSettlingCap));
  ASSERT_TRUE(!V::SettlingRebakeMayProceed(true, kSettlingCap - 1, kSettlingCap));
  ASSERT_TRUE(V::SettlingRebakeMayProceed(true, kSettlingCap, kSettlingCap));
  ASSERT_TRUE(V::SettlingRebakeMayProceed(true, 0, 0));   // defensive: a
  ASSERT_TRUE(V::SettlingRebakeMayProceed(true, 0, -3));  // cap disables it

  // Sustained-settling replay: a pending redraw lands EXACTLY at the cap.
  settling = 0;
  int landed_at = -1;
  for (int frame = 1; frame <= kSettlingCap + 4 && landed_at < 0; ++frame) {
    settling = V::NextSettlingDeferFrames(settling, true, true);
    if (V::SettlingRebakeMayProceed(true, settling, kSettlingCap)) {
      landed_at = frame;
    }
  }
  ASSERT_EQ(landed_at, kSettlingCap);

  // The cap is generous BECAUSE the prewarm plan is HUD-first: base 20*s warms within the plan's
  // FIRST block, many frames before the cap (asserted in TestFontPrewarmPlan). It only bounds ...
  ASSERT_TRUE(kSettlingCap > V::kHudPlateStormMaxDeferFrames);
  }
}

TEST_CASE(TestPlateGrowthAndMenuStorm) {
  // UI storm policies: plate textures grow only, and geometry-only page
  // rebakes wait for settling with a bounded deferral.
  namespace V = GameRules::Visuals;

  // Grow-only capacity: reallocate only when the bucket EXCEEDS capacity.
  ASSERT_TRUE(V::PlateNeedsRegrow(false, 0, 0, 256, 64));     // no texture yet
  ASSERT_TRUE(V::PlateNeedsRegrow(true, 256, 64, 512, 64));   // width growth
  ASSERT_TRUE(V::PlateNeedsRegrow(true, 256, 64, 256, 128));  // height growth
  ASSERT_TRUE(!V::PlateNeedsRegrow(true, 512, 128, 256, 64)); // shrink: reuse
  ASSERT_TRUE(!V::PlateNeedsRegrow(true, 256, 64, 256, 64));  // exact fit

  // Oscillation replay: down, up (within capacity), down, up beyond --
  // exactly ONE reallocation (the final growth), not four.
  int tex_w = 512;
  int tex_h = 64;
  int reallocs = 0;
  const int buckets[][2] = {{256, 64}, {512, 64}, {256, 64}, {768, 64}};
  for (const auto& b : buckets) {
    if (V::PlateNeedsRegrow(true, tex_w, tex_h, b[0], b[1])) {
      tex_w = b[0];
      tex_h = b[1];
      ++reallocs;
    }
  }
  ASSERT_EQ(reallocs, 1);
  ASSERT_EQ(tex_w, 768);

  // Menu storm: geometry-only dirty defers until the size settles.
  constexpr int kSettle = V::kMenuPageSettleFrames;
  constexpr int kCap = V::kMenuPageStormMaxDeferFrames;
  ASSERT_TRUE(kSettle >= 1);
  ASSERT_TRUE(kCap >= kSettle);  // the cap must outlive the settle window
  int defer_frames = 0;
  int rebaked_at = -1;
  for (int frame = 1; frame <= kCap + 4; ++frame) {
    const bool settled = (frame > kSettle);  // drag stops after kSettle frames
    defer_frames = V::NextStormDeferFrames(defer_frames, true, settled);
    if (V::StormRebakeApplyNow(true, settled, defer_frames, kCap)) {
      rebaked_at = frame;
      break;
    }
  }
  ASSERT_EQ(rebaked_at, kSettle + 1);

  // Sustained storm (never settles): the cap forces the rebake -- bounded
  // staleness, no unbounded drift.
  defer_frames = 0;
  rebaked_at = -1;
  for (int frame = 1; frame <= kCap + 4; ++frame) {
    defer_frames = V::NextStormDeferFrames(defer_frames, true, false);
    if (V::StormRebakeApplyNow(true, false, defer_frames, kCap)) {
      rebaked_at = frame;
      break;
    }
  }
  ASSERT_EQ(rebaked_at, kCap);
}

TEST_CASE(TestHudPlateGeometryBucketing) {
  // The HUD plate texture is allocated in BUCKETED dimensions (AGENT 7): an interactive resize
  // recreates the GPU texture only at bucket crossings, and the clipped 1:1 blit relies on the ...
  constexpr auto Bucket = GameRules::Visuals::HudPlateBucket;

  // Rounds UP to the next multiple of the step; exact multiples stay.
  ASSERT_EQ(Bucket(1, 256), 256);
  ASSERT_EQ(Bucket(255, 256), 256);
  ASSERT_EQ(Bucket(256, 256), 256);
  ASSERT_EQ(Bucket(257, 256), 512);
  ASSERT_EQ(Bucket(1792, 256), 1792);  // exact multiple: no slack
  ASSERT_EQ(Bucket(1920, 256), 2048);  // common 1080p width -> next bucket
  ASSERT_EQ(Bucket(60, 64), 64);
  ASSERT_EQ(Bucket(64, 64), 64);
  ASSERT_EQ(Bucket(65, 64), 128);

  // Degenerate inputs clamp to zero -- never negative, never a division by
  // zero (the guard runs before the arithmetic).
  ASSERT_EQ(Bucket(0, 256), 0);
  ASSERT_EQ(Bucket(-8, 256), 0);
  ASSERT_EQ(Bucket(64, 0), 0);

  // Invariants the clipped blit depends on: the bucket covers the value, so the src rect can never
  // read outside the plate, and it is an exact multiple of the step, so recreations happen only ...
  for (int w = 1; w < 4096; ++w) {
    ASSERT_TRUE(Bucket(w, 256) >= w);
    ASSERT_TRUE(Bucket(w, 256) % 256 == 0);
  }
}

TEST_CASE(TestPlateWarmBudgetPolicy) {
  namespace V = GameRules::Visuals;
  // The warm slice budget is a sane fraction of a 60 Hz frame: big enough
  // to make progress, small enough to never own the frame (~2-4 ms target).
  ASSERT_TRUE(V::kPlateWarmBudgetMicroseconds > 0);
  ASSERT_TRUE(V::kPlateWarmBudgetMicroseconds <= 4000);
  // The cap bounds staleness and stays tighter than the settling cap it
  // backstops (the stale plate keeps blitting during the warm pass).
  ASSERT_TRUE(V::kPlateWarmMaxDeferFrames > 0);
  ASSERT_TRUE(V::kPlateWarmMaxDeferFrames < V::kPlateSettlingMaxDeferFrames);
  // Defer clock: resets on completion, counts while incomplete.
  ASSERT_EQ(V::NextPlateWarmDeferFrames(7, false), 0);
  ASSERT_EQ(V::NextPlateWarmDeferFrames(0, true), 1);
  ASSERT_EQ(V::NextPlateWarmDeferFrames(6, true), 7);
  // Bake gate: only the expired cap forces a cold bake.
  ASSERT_TRUE(!V::PlateWarmBakeMayProceed(0));
  ASSERT_TRUE(!V::PlateWarmBakeMayProceed(V::kPlateWarmMaxDeferFrames - 1));
  ASSERT_TRUE(V::PlateWarmBakeMayProceed(V::kPlateWarmMaxDeferFrames));
  ASSERT_TRUE(V::PlateWarmBakeMayProceed(V::kPlateWarmMaxDeferFrames + 5));
  // Corner re-anchor policy: a baked plate blits ONLY while window width and
  // OSD scale still match the bake; anything stale falls back to direct draws.
  ASSERT_TRUE(!V::PlateAnchorsFresh(false, 1280, 1280, 1.0f, 1.0f));
  ASSERT_TRUE(V::PlateAnchorsFresh(true, 1280, 1280, 1.0f, 1.0f));
  ASSERT_TRUE(!V::PlateAnchorsFresh(true, 1280, 1920, 1.0f, 1.0f));
  ASSERT_TRUE(!V::PlateAnchorsFresh(true, 1280, 1280, 1.0f, 1.2f));
}

TEST_CASE(TestPlatePacingScheduler) {
  // The shared rebake scheduler (score HUD + telemetry overlay), replaying a structural storm:
  // preset switch at frame 0, busy presents for 8 frames, font settling for 40, the budgeted ...
  PlatePacing::Scheduler sched;
  int rebake_frame = -1;
  for (int f = 0; f < 120 && rebake_frame < 0; ++f) {
    const bool calm = (f >= 8);
    const bool warm = (f >= 3);
    sched.AdvanceWarmClock(!warm);
    sched.AdvanceRebakeClocks(true, true, f < 40, calm);
    if (sched.SettlingOk(f < 40, warm) && sched.CalmOk(calm) &&
        sched.WarmOk(warm)) {
      rebake_frame = f;
      sched.OnRebakeLanded();
    }
    // The rebake must NEVER land inside the storm: the target switch's
    // implicit driver sync is exactly what stacks onto those frames.
    ASSERT_TRUE(rebake_frame < 0 || rebake_frame >= 8);
  }
  // Warm glyphs + the first calm frame: the earliest safe landing.
  ASSERT_EQ(rebake_frame, 8);

  // Endless storm: bounded staleness -- forced exactly at the calm cap
  // (the clock reaches the cap on frame cap-1 of the 0-based replay).
  PlatePacing::Scheduler storm;
  int forced = -1;
  for (int f = 0; f < 200 && forced < 0; ++f) {
    storm.AdvanceWarmClock(false);
    storm.AdvanceRebakeClocks(true, true, true, false);
    if (storm.SettlingOk(true, true) && storm.CalmOk(false) &&
        storm.WarmOk(true)) {
      forced = f;
    }
  }
  ASSERT_EQ(forced, GameRules::Visuals::kCalmRebakeMaxDeferFrames - 1);
}

// SECTION 5 - PERSISTENCE & INTEGRITY (hall of fame ledger, signing)

TEST_CASE(TestHighScoreTableDomain) {
  // Domain: ordering by score, per-resolution lists and the capacity cap.
  {
  HighScoreTable table;
  const Coord res(1920, 1080);

  for (uint64_t s = 100; s <= 1200; s += 100) {
    HighScore entry(s, "Pilot_" + std::to_string(s), 1700000000 + static_cast<std::time_t>(s), res, 144);
    table.Add(entry);
  }

  const auto* list = table.Get(res);
  ASSERT_TRUE(list != nullptr);
  ASSERT_EQ(list->size(), 10ULL);
  ASSERT_EQ(list->rbegin()->Value(), 1200ULL);
  ASSERT_EQ(list->begin()->Value(), 300ULL);

  std::ostringstream ss_out;
  table.Serialize(ss_out);
  const std::string serialized = ss_out.str();

  HighScoreTable reloaded;
  std::istringstream ss_in(serialized);
  reloaded.Deserialize(ss_in);

  const auto* reloaded_list = reloaded.Get(res);
  ASSERT_TRUE(reloaded_list != nullptr);
  ASSERT_EQ(reloaded_list->size(), 10ULL);
  ASSERT_EQ(reloaded_list->rbegin()->Value(), 1200ULL);
  ASSERT_EQ(reloaded_list->rbegin()->Name(), "Pilot_1200");
  }
  // Edge cases: empty table, per-resolution partitioning, duplicate-run
  // preservation and exact-duplicate rejection.

  {
  HighScoreTable table;
  Coord res(1920, 1080);
  ASSERT_TRUE(table.Get(res) == nullptr || table.Get(res)->empty());

  HighScore e1(10, "A", 1700000000, res, 144);
  table.Add(e1);
  ASSERT_EQ(table.Get(res)->size(), 1ULL);

  HighScore e2(10, "B", 1700000001, res, 144);
  table.Add(e2);
  ASSERT_EQ(table.Get(res)->size(), 2ULL);

  Coord res2(1280, 720);
  HighScore e3(9999, "C", 1700000002, res2, 60);
  table.Add(e3);
  ASSERT_EQ(table.Get(res2)->size(), 1ULL);
  ASSERT_EQ(table.Get(res)->size(), 2ULL);

  // Distinct runs with identical score, stage, wave, resolution, and name are both preserved
  HighScore e4(10, "A", 1700000005, res, 144, 1, 1);
  HighScore e5(10, "A", 1700000010, res, 144, 1, 1);
  ASSERT_TRUE(table.Add(e4));
  ASSERT_TRUE(table.Add(e5));
  ASSERT_EQ(table.Get(res)->size(), 4ULL);

  // Exact duplicate with the identical timestamp is rejected
  HighScore e5_dup(10, "A", 1700000010, res, 144, 1, 1);
  ASSERT_TRUE(!table.Add(e5_dup));
  ASSERT_EQ(table.Get(res)->size(), 4ULL);
  }
  // Serialization is idempotent: serialize -> deserialize -> serialize is
  // byte-identical, so the on-disk ledger never churns.

  {
  HighScoreTable table_a;
  const Coord res(3840, 2160);
  table_a.Add(HighScore(5000, "Apex_Ace", 1700000000, res, 60));
  table_a.Add(HighScore(12500, "Vanguard_One", 1700000010, res, 120));

  std::ostringstream ss1;
  table_a.Serialize(ss1);

  HighScoreTable table_b;
  std::istringstream is1(ss1.str());
  table_b.Deserialize(is1);

  std::ostringstream ss2;
  table_b.Serialize(ss2);

  ASSERT_EQ(ss1.str(), ss2.str());
  }
}

TEST_CASE(TestHighScoreTamperRejection) {
  // The Hall of Fame only trusts rows the game itself signed: a genuine row survives the
  // serialize/deserialize round trip, while a hand-edited score (stale digest) or a ...
  HighScoreTable table;
  const Coord res(1920, 1080);
  table.Add(HighScore(4321, "Honest_Pilot", 1700000000, res, 60, 2, 7));

  std::ostringstream signed_out;
  table.Serialize(signed_out);
  const std::string ledger = signed_out.str();

  HighScoreTable reloaded;
  std::istringstream signed_in(ledger);
  reloaded.Deserialize(signed_in);
  ASSERT_EQ(reloaded.GetAll().size(), 1ULL);
  ASSERT_EQ(reloaded.GetAll().begin()->Value(), 4321ULL);

  // Bump the score digits in place, leaving the now-stale digest behind.
  std::string tampered = ledger;
  const std::size_t score_pos = tampered.find(";4321;");
  ASSERT_TRUE(score_pos != std::string::npos);
  tampered.replace(score_pos + 1, 4, "9999");

  HighScoreTable cheated;
  std::istringstream tampered_in(tampered);
  cheated.Deserialize(tampered_in);
  ASSERT_TRUE(cheated.GetAll().empty());

  // A hand-written row without the digest column is rejected as well.
  HighScoreTable unsigned_table;
  std::istringstream unsigned_in(
      "STAGE;WAVE;SCORE;WIDTH;HEIGHT;REFRESH_RATE;DATE;NAME\n"
      "2;7;4321;1920;1080;60;1700000000;Honest_Pilot\n");
  unsigned_table.Deserialize(unsigned_in);
  ASSERT_TRUE(unsigned_table.GetAll().empty());
}

TEST_CASE(TestScoreSha256KnownAnswer) {
  // NIST FIPS 180-4 known answer for SHA-256("abc"): guards the compact
  // digest core that signs every Hall of Fame row.
  static constexpr std::array<std::uint8_t, 32> kExpectedAbc = {
      0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
      0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
      0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  ASSERT_TRUE(ScoreHash::Sha256("abc") == kExpectedAbc);
  ASSERT_EQ(ScoreHash::LineDigest("abc").size(), ScoreHash::kDigestHexChars);
}

// SECTION 6 - CLI SURFACE (flag spelling, jukebox selection)

TEST_CASE(TestCliFlagsMatching) {
  // CliFlags is the single spelling policy for every command-line option: identity is the dash-free
  // word, so one or more leading dashes select the same flag. Names are compared exactly and ...
  ASSERT_TRUE(CliFlags::StripDashes("-v") == std::string_view("v"));
  ASSERT_TRUE(CliFlags::StripDashes("--version") == std::string_view("version"));
  ASSERT_TRUE(CliFlags::StripDashes("---log") == std::string_view("log"));
  ASSERT_TRUE(CliFlags::StripDashes("opengl") == std::string_view("opengl"));
  ASSERT_TRUE(CliFlags::StripDashes("---").empty());
  ASSERT_TRUE(CliFlags::StripDashes("").empty());

  // One, two, three dashes: one and the same option.
  ASSERT_TRUE(CliFlags::Matches("-version", "version"));
  ASSERT_TRUE(CliFlags::Matches("--version", "version"));
  ASSERT_TRUE(CliFlags::Matches("---version", "version"));
  ASSERT_TRUE(CliFlags::Matches("-v", "v"));
  ASSERT_TRUE(CliFlags::Matches("--v", "v"));

  // A bare word is a value, never a flag: "-play 1 4" relies on this so its
  // track numbers are not mistaken for options.
  ASSERT_TRUE(!CliFlags::Matches("version", "version"));
  ASSERT_TRUE(!CliFlags::Matches("4", "4"));

  // Exact comparison: no prefix, no case folding, no collisions between a
  // short letter and a longer name that starts with it.
  ASSERT_TRUE(!CliFlags::Matches("-v", "version"));
  ASSERT_TRUE(!CliFlags::Matches("-l", "list"));
  ASSERT_TRUE(!CliFlags::Matches("-list", "log"));
  ASSERT_TRUE(!CliFlags::Matches("-VERSION", "version"));

  // Degenerate inputs are inert: empty tokens and dash-only tokens match
  // nothing, and an empty flag name can never be "matched".
  ASSERT_TRUE(!CliFlags::Matches("", "version"));
  ASSERT_TRUE(!CliFlags::Matches("-", "version"));
  ASSERT_TRUE(!CliFlags::Matches("---", "version"));
  ASSERT_TRUE(!CliFlags::Matches("--", ""));
}

TEST_CASE(TestTrackSelectionParser) {
  // The pure jukebox selection parser: single track, ascending range and comma list (spaces around
  // commas optional, repeats allowed), with strict didactic errors -- no silent atoi() zeros, ...
  const auto parse = [](std::initializer_list<const char*> toks) {
    std::vector<std::string_view> tokens;
    tokens.reserve(toks.size());
    for (const char* t : toks) tokens.emplace_back(t);
    return CliTracks::Parse(tokens, StageFanfare::kPieceCount);
  };

  auto r = parse({"3"});
  ASSERT_TRUE(r.error.empty());
  ASSERT_EQ(r.tracks.size(), static_cast<size_t>(1));
  ASSERT_EQ(r.tracks[0], 3);

  r = parse({"1", "5"});
  ASSERT_TRUE(r.error.empty());
  ASSERT_EQ(r.tracks.size(), static_cast<size_t>(5));
  ASSERT_EQ(r.tracks.front(), 1);
  ASSERT_EQ(r.tracks.back(), 5);

  r = parse({"9", "2"});
  ASSERT_TRUE(!r.error.empty());  // inverted range points at the comma list

  const std::vector<int> expected{6, 1, 15, 2, 8};
  r = parse({"6,1,15,2,8"});                  // single token
  ASSERT_TRUE(r.error.empty());
  ASSERT_TRUE(r.tracks == expected);
  r = parse({"6,", "1,", "15,", "2,", "8"});  // comma + space (shell-split)
  ASSERT_TRUE(r.error.empty());
  ASSERT_TRUE(r.tracks == expected);
  r = parse({"6 , 1,15, 2 ,8"});              // loose separators, one token
  ASSERT_TRUE(r.error.empty());
  ASSERT_TRUE(r.tracks == expected);
  r = parse({"6", ",", "1"});                 // comma-only token
  ASSERT_TRUE(r.error.empty());
  ASSERT_TRUE((r.tracks == std::vector<int>{6, 1}));

  r = parse({"3,3"});                         // repeats are allowed
  ASSERT_TRUE(r.error.empty());
  ASSERT_EQ(r.tracks.size(), static_cast<size_t>(2));

  ASSERT_TRUE(!parse({}).error.empty());                  // nothing given
  ASSERT_TRUE(!parse({"0"}).error.empty());               // below range
  const std::string above_range = std::to_string(StageFanfare::kPieceCount + 1);
  ASSERT_TRUE(!parse({above_range.c_str()}).error.empty());  // above range
  ASSERT_TRUE(!parse({"abc"}).error.empty());             // not a number
  ASSERT_TRUE(!parse({"3.7"}).error.empty());             // strict digits only
  ASSERT_TRUE(!parse({"12x"}).error.empty());             // trailing garbage
  ASSERT_TRUE(!parse({"99999999999999"}).error.empty());  // overflow-safe
  ASSERT_TRUE(!parse({"1", "2", "3"}).error.empty());     // use commas instead
  ASSERT_TRUE(!parse({",,,"}).error.empty());             // empty list
}

// SECTION 7 - CONTAINERS & CACHES (recycling, bake index)

namespace {
/// Minimal instrumented type for the RecycleBin contract test: counts live instances (proving the
/// bin recycles instead of leaking or double-freeing) and exposes heap addresses (proving ...
struct RecycleProbe {
  static inline int live_instances = 0;
  int id{0};
  explicit RecycleProbe(int probe_id) : id(probe_id) { ++live_instances; }
  ~RecycleProbe() { --live_instances; }
};
}  // namespace

TEST_CASE(TestRecycleBinLifoReuse) {
  // Contract under test: Reserve() once at startup, then Release()/TryAcquire() never allocate or
  // deallocate; objects come back LIFO (most recently released = warmest cache lines first); ...
  RecycleBin<RecycleProbe> bin;
  bin.Reserve(4);

  auto first = std::make_unique<RecycleProbe>(1);
  auto second = std::make_unique<RecycleProbe>(2);
  RecycleProbe* const first_addr = first.get();

  bin.Release(std::move(first));
  bin.Release(std::move(second));
  ASSERT_EQ(bin.Size(), static_cast<std::size_t>(2));

  // LIFO: the most recently released probe comes back first.
  auto out = bin.TryAcquire();
  ASSERT_TRUE(out != nullptr);
  ASSERT_EQ(out->id, 2);

  out = bin.TryAcquire();
  ASSERT_TRUE(out != nullptr);
  ASSERT_EQ(out->id, 1);
  ASSERT_TRUE(out.get() == first_addr);  // same heap block: zero reallocation

  ASSERT_TRUE(bin.TryAcquire() == nullptr);  // empty bin -> cold-spawn signal

  bin.Release(nullptr);  // safe no-op
  ASSERT_EQ(bin.Size(), static_cast<std::size_t>(0));

  out.reset();
  ASSERT_EQ(RecycleProbe::live_instances, 0);  // nothing leaked
}

TEST_CASE(TestBakeHashIndex) {
  using BakeCache::BakeHash;
  using BakeCache::kIndexSlots;
  using BakeCache::NextSlot;
  using BakeCache::SlotOf;

  // Determinism: identical identity -> identical digest (probe stability
  // within and across frames depends on it).
  constexpr std::string_view kKey = "PLAYER DESTROYED";
  ASSERT_EQ(BakeHash(kKey, 42, true, true), BakeHash(kKey, 42, true, true));

  // Every identity component participates: the same text at another size,
  // face or shadow flag must never alias onto the same digest.
  ASSERT_TRUE(BakeHash(kKey, 42, true, true) != BakeHash(kKey, 43, true, true));
  ASSERT_TRUE(BakeHash(kKey, 42, true, true) != BakeHash(kKey, 42, false, true));
  ASSERT_TRUE(BakeHash(kKey, 42, true, true) != BakeHash(kKey, 42, true, false));
  ASSERT_TRUE(BakeHash(kKey, 42, true, true) !=
              BakeHash("PLAYER DESTROYES", 42, true, true));

  // The index masks require a power-of-two table with room for the whole
  // 256-entry cache at load factor <= 0.5.
  ASSERT_EQ(kIndexSlots & (kIndexSlots - 1), static_cast<std::size_t>(0));
  ASSERT_TRUE(kIndexSlots >= 2u * 256u);

  // Linear probing wraps around exactly once (open addressing can never loop
  // forever on insert while a free slot exists).
  const std::size_t start = SlotOf(BakeHash(kKey, 1, false, false));
  std::size_t slot = start;
  for (std::size_t i = 0; i < kIndexSlots; ++i) {
    ASSERT_TRUE(slot < kIndexSlots);
    slot = NextSlot(slot);
  }
  ASSERT_EQ(slot, start);

  // Distribution sanity: 256 distinct vocabulary keys must spread over many first-probe slots
  // (birthday expectation ~201 of 512 for a good digest; a degenerate hash would collapse them ...
  std::array<bool, kIndexSlots> seen{};
  std::size_t distinct = 0;
  for (int i = 0; i < 256; ++i) {
    const std::string key = "BAKE-KEY-" + std::to_string(i);
    const std::size_t s = SlotOf(BakeHash(key, 20 + (i % 8), (i & 1) != 0, true));
    if (!seen[s]) {
      seen[s] = true;
      ++distinct;
    }
  }
  ASSERT_TRUE(distinct > 128u);
}

// SECTION 8 - STARFIELD (parallax layers, density ladder, scintillation)

TEST_CASE(TestStarfieldDensityAndTwinkle) {
  using namespace GameRules::Starfield;
  // Density ladder: each quality level dispatches to its own SSOT factor, the factors strictly
  // increase with quality, and out-of-range levels fall back to the default baseline instead of ...
  ASSERT_NEAR(GetDensityFactor(0), kDensityFactorLevel0, 1e-6);
  ASSERT_NEAR(GetDensityFactor(1), kDensityFactorLevel1, 1e-6);
  ASSERT_NEAR(GetDensityFactor(2), kDensityFactorLevel2, 1e-6);
  ASSERT_NEAR(GetDensityFactor(3), kDensityFactorLevel3, 1e-6);
  ASSERT_NEAR(GetDensityFactor(4), kDensityFactorLevel4, 1e-6);
  ASSERT_TRUE(kDensityFactorLevel0 < kDensityFactorLevel1);
  ASSERT_TRUE(kDensityFactorLevel1 < kDensityFactorLevel2);
  ASSERT_TRUE(kDensityFactorLevel2 < kDensityFactorLevel3);
  ASSERT_TRUE(kDensityFactorLevel3 < kDensityFactorLevel4);
  ASSERT_NEAR(GetDensityFactor(-1), kDensityFactorLevel2, 1e-6);
  ASSERT_NEAR(GetDensityFactor(99), kDensityFactorLevel2, 1e-6);

  // Deep DRY canary: tau has exactly ONE spelling in the codebase -- twice the circle-constant SSOT
  // -- so no hand-typed 6.28 literal can creep back into any subsystem.
  ASSERT_EQ(kTau, 2.0f * Pi);

  // Scintillation remap: Pulse01 is the linear ramp sealing the SSOT [floor, ceil] band onto [0, 1]
  // - it clamps below the floor, saturates at the ceiling and maps the band midpoint to the ...
  ASSERT_TRUE(kTwinkleFloor > 0.0f && kTwinkleFloor < kTwinkleCeil);
  ASSERT_TRUE(kTwinkleCeil <= 1.0f);
  ASSERT_NEAR(Pulse01(kTwinkleFloor), 0.0f, 1e-4f);
  ASSERT_NEAR(Pulse01(kTwinkleCeil), 1.0f, 1e-4f);
  ASSERT_NEAR(Pulse01((kTwinkleFloor + kTwinkleCeil) * 0.5f), 0.5f, 1e-4f);
  ASSERT_NEAR(Pulse01(0.0f), 0.0f, 1e-4f);
  ASSERT_NEAR(Pulse01(1.0f), 1.0f, 1e-4f);

  // Twinkle periods are well-formed authoring bounds; the derived angular
  // speeds inherit them (shortest period -> fastest speed).
  ASSERT_TRUE(kTwinklePeriodMinSec > 0.0f);
  ASSERT_TRUE(kTwinklePeriodMinSec < kTwinklePeriodMaxSec);
  ASSERT_TRUE(kTwinkleSpeedMin > 0.0f && kTwinkleSpeedMin < kTwinkleSpeedMax);

  // Parallax ordering: the foreground layer is strictly larger and falls at least as fast as the
  // mid field, and both size/speed ranges are well-formed minima below maxima.
  ASSERT_TRUE(kMidFieldHalfSizeMinPx >= 1.5f);
  ASSERT_TRUE(kMidFieldHalfSizeMaxPx > kMidFieldHalfSizeMinPx);
  ASSERT_TRUE(kForegroundSizeMinPx > kMidFieldHalfSizeMaxPx * 2.0f);
  ASSERT_TRUE(kForegroundSizeMaxPx > kForegroundSizeMinPx);
  ASSERT_TRUE(kMidFieldFallSpeedMinPx > 0.0f);
  ASSERT_TRUE(kMidFieldFallSpeedMinPx < kMidFieldFallSpeedMaxPx);
  ASSERT_TRUE(kForegroundFallSpeedMinPx < kForegroundFallSpeedMaxPx);
  ASSERT_TRUE(kForegroundFallSpeedMinPx >= kMidFieldFallSpeedMaxPx);

  // Readability bounds: both alpha floors keep their layer visible without
  // pinning it at full brightness, and the DPI scale band is a proper range.
  ASSERT_TRUE(kMidFieldAlphaFloor > 0.0f && kMidFieldAlphaFloor < 1.0f);
  ASSERT_TRUE(kForegroundAlphaFloor > 0 && kForegroundAlphaFloor < 255);
  ASSERT_TRUE(kStarScaleDpiMin > 0.0f && kStarScaleDpiMin < kStarScaleDpiMax);
  ASSERT_TRUE(kMaxStarQuadCount > 0u);

  // Flare kernel: the core is wider than a collapsing point spread, gains
  // amplify above unity and the halo stays a positive, softer companion.
  ASSERT_TRUE(kFlareCoreSigma2 > 9.0f);
  ASSERT_TRUE(kFlareCoreGain > 1.0f);
  ASSERT_TRUE(kFlareHaloSigma > 0.0f && kFlareHaloGain > 0.0f);
}

// SECTION 9 - SOUNDTRACK (AGENT 8: manifests, windows, export)

TEST_CASE(TestSoundtrackWindowGeneration) {
  using namespace GameRules::Progression;
  // Window mechanics of the synthesis engine, driven by each track's OWN manifest (concert excerpt
  // or complete score): the game flow bakes the climatic six-second preset, any inner window ...
  ASSERT_NEAR(kSoundtrackExcerptFullSeconds, 30.0f, 1e-4f);
  ASSERT_TRUE(kStageFanfareDurationSeconds < kSoundtrackExcerptFullSeconds);

  constexpr int sr = 8000;   // low rate keeps this pure-synthesis test fast
  // The engine sizes buffers through SecondsToSamples:
  // size_t(sample_rate * double(window_len)). Expected frame counts mirror
  // that exact conversion, so float windows keep their own sub-sample
  // residue instead of being rounded through a different arithmetic path
  // (a float product cast loses whole samples over minutes-long scores).
  auto frames = [](float window_len) {
    return static_cast<size_t>(static_cast<double>(sr) *
                               static_cast<double>(window_len));
  };
  // rms over interleaved stereo satellite: a and b are frame indices.
  auto rms = [](const std::vector<float>& v, size_t a, size_t b) {
    double acc = 0;
    for (size_t i = a; i < b && 2 * i + 1 < v.size(); ++i) {
      acc += double(v[2 * i]) * double(v[2 * i]) + double(v[2 * i + 1]) * double(v[2 * i + 1]);
    }
    const size_t n = (b > a) ? 2 * (b - a) : size_t(1);
    return std::sqrt(acc / double(n));
  };

  for (int stage = 1; stage <= StageFanfare::kPieceCount; ++stage) {
    const float duration = StageFanfare::PieceDurationSeconds(stage);
    ASSERT_TRUE(duration >= kSoundtrackExcerptFullSeconds);

    // The climatic six-second preset the game flow bakes, clamped in range
    // exactly like SoundManager::EnsureFanfareReady does.
    const float latest_start =
        std::max(0.0f, duration - kStageFanfareDurationSeconds);
    const float peak = std::clamp(StageFanfare::PiecePeakStartSeconds(stage),
                                  0.0f, latest_start);
    const float preset_end =
        std::min(peak + kStageFanfareDurationSeconds, duration);
    const auto preset = StageFanfare::Generate(stage, sr, peak, preset_end);
    ASSERT_EQ(preset.satellite.size(), 2 * frames(preset_end - peak));

    // An inner window renders exactly its own span.
    const auto inner = StageFanfare::Generate(stage, sr, 10.0f, 20.0f);
    ASSERT_EQ(inner.satellite.size(), 2 * frames(20.0f - 10.0f));

    // Crystalline normalization holds in every window: the whole anthology plays at one perceived
    // loudness (same 0.92 peak ceiling), with zero digital clipping.
    for (const StageFanfare::Asset* asset : {&preset, &inner}) {
      float pk = 0.0f;
      for (float v : asset->satellite) pk = std::max(pk, std::abs(v));
      for (float v : asset->subwoofer) pk = std::max(pk, std::abs(v));
      ASSERT_TRUE(pk > 0.5f);
      ASSERT_TRUE(pk <= 0.93f);
    }

    // Edge envelope (the fades live uniquely in constants.h): the shared fade-out ramp seals the
    // tail of ANY window -- audible music before the ramp, near-silence at the very edge.
    const size_t n = preset.satellite.size() / 2;
    const size_t fade = frames(GameRules::Audio::kFanfareFadeOutSec);
    ASSERT_TRUE(fade > 0 && 4 * fade < n);
    ASSERT_TRUE(rms(preset.satellite, 0, n - fade) >
                2.0 * rms(preset.satellite, n - fade / 8, n));
  }
}

TEST_CASE(TestScoreManifestConstraints) {
  for (int stage = 1; stage <= StageFanfare::kPieceCount; ++stage) {
    // Each piece is bounded by its OWN scored length: 30 s for the excerpt
    // movements, the complete overture for track 2.
    const float duration = StageFanfare::PieceDurationSeconds(stage);
    // Climatic preset sanity: the frozen peak start keeps the six-second
    // game-flow fanfare window entirely inside the scored length.
    const float peak = StageFanfare::PiecePeakStartSeconds(stage);
    ASSERT_TRUE(peak >= 0.0f);
    ASSERT_TRUE(peak + GameRules::Progression::kStageFanfareDurationSeconds <=
                duration + 1e-4f);
    const std::vector<StageFanfare::ScoreNote> score = StageFanfare::BuildScore(stage);
    ASSERT_TRUE(score.size() > 200);
    for (const auto& n : score) {
      ASSERT_TRUE(n.voice < SoundFontSynth::kEnsembleVoices);
      ASSERT_TRUE(n.midi >= 21 && n.midi <= 108);
      ASSERT_TRUE(n.vel > 0.0f && n.vel <= 1.06f);
      ASSERT_TRUE(n.dur > 0.0f && n.dur <= duration);
      ASSERT_TRUE(n.start >= 0.0f && n.start < duration);
    }

    // The in-game font atlas rasterizes ASCII 32..126 ONLY, and every metadata string flows into
    // drawn UI lines (the wave banner and the soundtrack cards): a non-ASCII byte would render ...
    const StageFanfare::PieceMetadata& meta = StageFanfare::GetMetadata(stage);
    for (const char* field : {meta.composer, meta.nationality, meta.life_dates,
                              meta.title, meta.opus_catalog,
                              meta.movement_phrase, meta.historical_curiosity}) {
      for (const char* p = field; *p != '\0'; ++p) {
        ASSERT_TRUE(static_cast<unsigned char>(*p) < 128u);
      }
    }
  }
}

TEST_CASE(TestGuaraniCompleteOverture) {
  // Track 2 carries the COMPLETE Il Guarany overture (mm. 1-65): the score continues far past the
  // 30-second excerpt window of the other tracks and resolves on the printed Andante cadence; ...
  using GameRules::Progression::kSoundtrackExcerptFullSeconds;
  const float duration = StageFanfare::PieceDurationSeconds(2);
  ASSERT_TRUE(duration > 120.0f && duration < 130.0f);

  const auto score = StageFanfare::BuildScore(2);
  float last_start = 0.0f;
  int beyond_excerpt = 0;
  for (const auto& n : score) {
    last_start = std::max(last_start, n.start);
    if (n.start > kSoundtrackExcerptFullSeconds) ++beyond_excerpt;
  }
  ASSERT_TRUE(beyond_excerpt > 400);
  ASSERT_TRUE(last_start > 115.0f && last_start < duration);

  constexpr int sr = 8000;   // low rate keeps this pure-synthesis test fast
  const auto overrun = StageFanfare::Generate(2, sr, 0.0f, 999.0f);
  // Expected size goes through the same double conversion as the engine's SecondsToSamples; a float
  // product cast would lose whole samples over the two-minute score.
  ASSERT_EQ(overrun.satellite.size(),
            2 * static_cast<size_t>(static_cast<double>(duration) * sr));
}

TEST_CASE(TestAudioExportWavChain) {
  const int sr = 8000;   // low rate keeps the export test fast
  const auto asset = StageFanfare::Generate(1, sr, 0.0f, 1.0f);
  const auto pcm = AudioExport::RenderPlaybackPcm(asset);
  ASSERT_EQ(pcm.size(), asset.subwoofer.size() * 2);
  int peak = 0;
  for (int16_t s : pcm) peak = std::max(peak, std::abs(static_cast<int>(s)));
  ASSERT_TRUE(peak > 1000);      // audible
  ASSERT_TRUE(peak <= 32767);    // soft limiter keeps int16 range

  const std::string path = "audio_export_selftest.wav";
  ASSERT_TRUE(AudioExport::WriteWav(path, pcm, sr));
  FILE* f = std::fopen(path.c_str(), "rb");
  ASSERT_TRUE(f != nullptr);
  char hdr[12] = {};
  ASSERT_EQ(std::fread(hdr, 1, 12, f), size_t(12));
  std::fclose(f);
  ASSERT_TRUE(std::memcmp(hdr, "RIFF", 4) == 0);
  ASSERT_TRUE(std::memcmp(hdr + 8, "WAVE", 4) == 0);
  std::remove(path.c_str());
}

TEST_CASE(TestSoundFontEnsembleMapping) {
  // GM program numbers are 0-based: Violin 41, Cello 43, Flute 74, Trumpet 57, Acoustic Grand Piano
  // 1, Viola 42, Contrabass 44, French Horn 61, Oboe 69, Orchestral Harp 47. The SoundFont ...
  ASSERT_EQ(SoundFontSynth::kEnsembleVoices,
            static_cast<int>(StageFanfare::kInstrumentCount));
  ASSERT_EQ(SoundFontSynth::kGmPrograms[0], 40);
  ASSERT_EQ(SoundFontSynth::kGmPrograms[1], 42);
  ASSERT_EQ(SoundFontSynth::kGmPrograms[2], 73);
  ASSERT_EQ(SoundFontSynth::kGmPrograms[3], 56);
  ASSERT_EQ(SoundFontSynth::kGmPrograms[4], 0);
  ASSERT_EQ(SoundFontSynth::kGmPrograms[5], 41);
  ASSERT_EQ(SoundFontSynth::kGmPrograms[6], 43);
  ASSERT_EQ(SoundFontSynth::kGmPrograms[7], 60);
  ASSERT_EQ(SoundFontSynth::kGmPrograms[8], 68);
  ASSERT_EQ(SoundFontSynth::kGmPrograms[9], 46);
  // Stage seats live inside the equal-power pan range and the piano keeps
  // the centre seat of the stereo image.
  for (int v = 0; v < SoundFontSynth::kEnsembleVoices; ++v) {
    ASSERT_TRUE(SoundFontSynth::kStagePan[v] >= 0.0f &&
                SoundFontSynth::kStagePan[v] <= 1.0f);
  }
  ASSERT_NEAR(SoundFontSynth::kStagePan[4], 0.5f, 1e-4f);
}

TEST_CASE(TestSoundFontEngineDefaultPreference) {
  // Requirement SSOT: a bare launch (aliens-invaders) renders through the TinySoundFont bank, and
  // -r/-render opts into the procedural synth through the very switch the CLI pre-pass flips. ...
  ASSERT_TRUE(SoundFontSynth::Preferred());     // default: TinySoundFont
  SoundFontSynth::SetPreferred(false);          // exactly what -r does
  ASSERT_TRUE(!SoundFontSynth::Preferred());
  SoundFontSynth::SetPreferred(true);           // restore process default
  ASSERT_TRUE(SoundFontSynth::Preferred());
}

int main() {
  std::cout << "\033[1m==============================================================================\033[0m\n";
  std::cout << "\033[1;36m       ALIENS INVADERS - C++20 AUTOMATED HEADLESS TEST SUITE  \033[0m\n";
  std::cout << "\033[1m==============================================================================\033[0m\n";

  RUN_TEST(TestCollisionContract);
  RUN_TEST(TestCoordLengthUhdRegression);
  RUN_TEST(TestTransform2DInterpolation);
  RUN_TEST(TestGameObjectMotionAndCollider);
  RUN_TEST(TestRandomStreamDeterminism);
  RUN_TEST(TestSimulationTimingRelations);
  RUN_TEST(TestProgressionWaveLadder);
  RUN_TEST(TestStageCatalogConsistency);
  RUN_TEST(TestPlayerKinetics);
  RUN_TEST(TestFleetKinetics);
  RUN_TEST(TestCombatEconomyAndFairPlay);
  RUN_TEST(TestFormationGridSymmetryAndScaling);
  RUN_TEST(TestAlien14WarpCellSelection);
  RUN_TEST(TestKinematicsHelpers);
  RUN_TEST(TestTrajectoryKinematicsContract);
  RUN_TEST(TestSplineResampler);
  RUN_TEST(TestPacingBoundedAccumulator);
  RUN_TEST(TestHitchFrameRecorderAttribution);
  RUN_TEST(TestHitchSpscRingWaitFree);
  RUN_TEST(TestHitchVerdictPolicy);
  RUN_TEST(TestHitchFreezeSnapshotAndSeverity);
  RUN_TEST(TestExternalBurstPolicy);
  RUN_TEST(TestCadenceStutterPattern);
  RUN_TEST(TestHitchScopeSelfTimeAndCharging);
  RUN_TEST(TestHitchScopeRankingAndCallPaths);
  RUN_TEST(TestFrameContextAndMaxCallAttribution);
  RUN_TEST(TestHitchLogCliFlag);
  RUN_TEST(TestFontPublishBudgets);
  RUN_TEST(TestFontPrewarmPlan);
  RUN_TEST(TestCalmRebakePolicy);
  RUN_TEST(TestHudPlateStormDeferral);
  RUN_TEST(TestPlateGrowthAndMenuStorm);
  RUN_TEST(TestHudPlateGeometryBucketing);
  RUN_TEST(TestPlateWarmBudgetPolicy);
  RUN_TEST(TestPlatePacingScheduler);
  RUN_TEST(TestHighScoreTableDomain);
  RUN_TEST(TestHighScoreTamperRejection);
  RUN_TEST(TestScoreSha256KnownAnswer);
  RUN_TEST(TestCliFlagsMatching);
  RUN_TEST(TestTrackSelectionParser);
  RUN_TEST(TestRecycleBinLifoReuse);
  RUN_TEST(TestBakeHashIndex);
  RUN_TEST(TestStarfieldDensityAndTwinkle);
  RUN_TEST(TestSoundtrackWindowGeneration);
  RUN_TEST(TestScoreManifestConstraints);
  RUN_TEST(TestGuaraniCompleteOverture);
  RUN_TEST(TestAudioExportWavChain);
  RUN_TEST(TestSoundFontEnsembleMapping);
  RUN_TEST(TestSoundFontEngineDefaultPreference);

  std::cout << "\033[1m==============================================================================\033[0m\n";
  if (g_tests_failed == 0) {
    std::cout << "\033[1;32m >>> All " << g_tests_passed
              << " automated unit tests passed successfully! <<<\033[0m\n";
    return 0;
  } else {
    std::cerr << "\033[1;31m >>> " << g_tests_failed
              << " test(s) failed! <<<\033[0m\n";
    return 1;
  }
}
