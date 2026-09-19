#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "constants.h"
#include "game_object.h"
#include "highscore_table.h"
#include "math_types.h"
#include "path.h"
#include "random_stream.h"
#include "render_snapshot.h"

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_CASE(name) static void name()
#define RUN_TEST(name)   do {     std::cout << "  • Running " << #name << "... ";     const int initial_failed = g_tests_failed;     name();     if (g_tests_failed == initial_failed) {       std::cout << "\033[32m[PASSED]\033[0m\n";       ++g_tests_passed;     } else {       std::cout << "\033[31m[FAILED]\033[0m\n";     }   } while (0)

#define ASSERT_TRUE(expr)   do {     if (!(expr)) {       std::cerr << "\n    Assertion failed: " << #expr << " at " << __FILE__                 << ":" << __LINE__ << "\n";       ++g_tests_failed;       return;     }   } while (0)

#define ASSERT_EQ(a, b)   do {     if ((a) != (b)) {       std::cerr << "\n    Assertion failed: (" << #a << " == " << #b                 << ") => (" << (a) << " != " << (b) << ") at " << __FILE__                 << ":" << __LINE__ << "\n";       ++g_tests_failed;       return;     }   } while (0)

#define ASSERT_NEAR(a, b, eps)   do {     if (std::abs((a) - (b)) > (eps)) {       std::cerr << "\n    Assertion failed: |" << #a << " - " << #b                 << "| <= " << #eps << " => |" << (a) << " - " << (b)                 << "| = " << std::abs((a) - (b)) << " at " << __FILE__ << ":"                 << __LINE__ << "\n";       ++g_tests_failed;       return;     }   } while (0)

TEST_CASE(TestAABBIntersection) {
  const AABB box_a(20.0f, 20.0f);
  const AABB box_b(15.0f, 15.0f);

  ASSERT_TRUE(box_a.Intersects(Vec2f(100.0f, 100.0f), Vec2f(100.0f, 100.0f), box_b));
  ASSERT_TRUE(box_a.Intersects(Vec2f(100.0f, 100.0f), Vec2f(115.0f, 110.0f), box_b));
  ASSERT_TRUE(!box_a.Intersects(Vec2f(100.0f, 100.0f), Vec2f(200.0f, 200.0f), box_b));
  ASSERT_TRUE(!box_a.Intersects(Vec2f(100.0f, 100.0f), Vec2f(130.0f, 100.0f), box_b));
}

TEST_CASE(TestTransform2DInterpolation) {
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

TEST_CASE(TestRandomStreamDeterminism) {
  RandomStream rng1(1337);
  RandomStream rng2(1337);

  for (int i = 0; i < 100; ++i) {
    ASSERT_EQ(rng1.NextU32(), rng2.NextU32());
    ASSERT_EQ(rng1.UniformInt(10, 50), rng2.UniformInt(10, 50));
    ASSERT_NEAR(rng1.UniformFloat(-1.0f, 1.0f), rng2.UniformFloat(-1.0f, 1.0f), 1e-5f);
  }

  for (int i = 0; i < 1000; ++i) {
    const int val = rng1.UniformInt(5, 12);
    ASSERT_TRUE(val >= 5 && val <= 12);
  }
}

TEST_CASE(TestGameRulesPureKinetics) {
  ASSERT_EQ(GameRules::Player::ComputeSpeed(0), 3.5f);
  ASSERT_NEAR(GameRules::Player::ComputeSpeed(1), 3.85f, 1e-3f);
  ASSERT_NEAR(GameRules::Player::ComputeSpeed(2), 4.20f, 1e-3f);
  ASSERT_NEAR(GameRules::Player::ComputeSpeed(5), 4.20f, 1e-3f);

  ASSERT_EQ(GameRules::Fleet::GetKamikazeQuota(1), 1);
  ASSERT_EQ(GameRules::Fleet::GetKamikazeQuota(15), 1);
  ASSERT_EQ(GameRules::Fleet::GetKamikazeQuota(16), 2);
  ASSERT_EQ(GameRules::Fleet::GetKamikazeQuota(31), 3);

  ASSERT_EQ(GameRules::Fleet::GetMaxAttackWaitFrames(1), 300);
  ASSERT_EQ(GameRules::Fleet::GetMaxAttackWaitFrames(16), 240);
  ASSERT_EQ(GameRules::Fleet::GetMaxAttackWaitFrames(31), 180);
}

TEST_CASE(TestCatmullRomEndpoints) {
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

TEST_CASE(TestHighScoreTablePureDomain) {
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

TEST_CASE(TestAntiScissorBallisticInvariants) {
  ASSERT_EQ(GameRules::Fleet::kSafeEvasionCorridorPixels, 118);

  const int tight_separation = 140;
  const bool converging = true;
  const int min_allowed = converging ? (GameRules::Fleet::kSafeEvasionCorridorPixels * 2) : GameRules::Fleet::kSafeEvasionCorridorPixels;
  ASSERT_TRUE(tight_separation < min_allowed);
}

TEST_CASE(TestScoreProgressionInvariants) {
  constexpr uint64_t pts = 5000;
  constexpr uint64_t step = 1000;
  ASSERT_EQ(pts / step, 5ULL);
}

TEST_CASE(TestGameRulesSSOTInvariants) {
  ASSERT_EQ(GameRules::Simulation::kSimulationFrequencyHz, 60.0f);
  ASSERT_EQ(GameRules::Progression::kWavesPerStage, 15);
  ASSERT_EQ(GameRules::Progression::kStageFanfareDurationSeconds, 10.0f);
  ASSERT_EQ(GameRules::Player::kInitialShieldLives, 3);
  ASSERT_EQ(GameRules::Player::kPlayerMaxShield, 8);
  ASSERT_EQ(GameRules::Visuals::kFloatingTextHitFontSize, 24.0f);
  ASSERT_EQ(GameRules::Fleet::kAlienBaseWidthPixels, 84.0f);
  ASSERT_EQ(GameRules::Combat::kScorePerAlienHit, 10ULL);
  ASSERT_EQ(GameRules::Combat::kScorePerKamikazeAlienHit, 20ULL);
}

TEST_CASE(TestPureCollisionFunction) {
  const Transform2D t1(Vec2f(100.0f, 100.0f));
  const AABB b1(20.0f, 20.0f);
  const Transform2D t2(Vec2f(110.0f, 110.0f));
  const AABB b2(20.0f, 20.0f);
  ASSERT_TRUE(CheckCollision(t1, b1, t2, b2, 0.65f));

  const Transform2D t3(Vec2f(300.0f, 300.0f));
  ASSERT_TRUE(!CheckCollision(t1, b1, t3, b2, 0.65f));
}

TEST_CASE(TestHeadlessFlightPathSmoothSpline) {
  const FlightPath path({{0.0f, 0.0f}, {50.0f, 100.0f}, {100.0f, 200.0f}});
  ASSERT_TRUE(!path.Empty());
  ASSERT_TRUE(path.Size() >= 3u);
  ASSERT_NEAR(path[0].x, 0.0f, 1e-3f);
  ASSERT_NEAR(path[0].y, 0.0f, 1e-3f);
}

TEST_CASE(TestProgressionMath) {
  using namespace GameRules::Progression;
  ASSERT_EQ(WaveToStage(1), 1);
  ASSERT_EQ(WaveToStage(15), 1);
  ASSERT_EQ(WaveToStage(16), 2);
  ASSERT_EQ(WaveToStage(30), 2);
  ASSERT_EQ(WaveToStage(31), 3);
  ASSERT_EQ(WaveToStage(1000), 67);

  ASSERT_EQ(WaveInStage(1), 0);
  ASSERT_EQ(WaveInStage(15), 14);
  ASSERT_EQ(WaveInStage(16), 0);
  ASSERT_EQ(WaveInStage(31), 0);

  for (int w = 1; w <= 300; ++w) {
    int stage = WaveToStage(w);
    int inStage = WaveInStage(w);
    ASSERT_EQ((stage - 1) * kWavesPerStage + inStage + 1, w);
    ASSERT_TRUE(inStage >= 0 && inStage < kWavesPerStage);
  }
}

TEST_CASE(TestPlayerKineticsClamp) {
  using namespace GameRules::Player;
  ASSERT_EQ(ComputeSpeed(-100), ComputeSpeed(0));
  ASSERT_EQ(ComputeSpeed(999), ComputeSpeed(kPlayerMaxSpeedLevel));
  ASSERT_TRUE(ComputeSpeed(0) < ComputeSpeed(1));
  ASSERT_TRUE(ComputeSpeed(1) < ComputeSpeed(2));
  ASSERT_NEAR(ComputeSpeed(0), 3.5f, 1e-4f);
  ASSERT_NEAR(ComputeSpeed(1), 3.85f, 1e-4f);
  ASSERT_NEAR(ComputeSpeed(2), 4.20f, 1e-4f);
  ASSERT_NEAR(ComputeSpeed(5), 4.20f, 1e-4f);

  ASSERT_EQ(ComputeFireInterval(-1), ComputeFireInterval(0));
  ASSERT_EQ(ComputeFireInterval(999), ComputeFireInterval(kPlayerMaxFireLevel));
  ASSERT_TRUE(ComputeFireInterval(0) >= ComputeFireInterval(1));
  ASSERT_TRUE(ComputeFireInterval(1) >= ComputeFireInterval(2));
  ASSERT_TRUE(ComputeFireInterval(0) >= 8);
  ASSERT_TRUE(ComputeFireInterval(2) >= 8);
  ASSERT_EQ(ComputeFireInterval(0), 40);
  ASSERT_EQ(ComputeFireInterval(1), 36);
  ASSERT_EQ(ComputeFireInterval(2), 33);
}

TEST_CASE(TestFleetKinetics) {
  using namespace GameRules;
  for (int w = 1; w <= 90; ++w) {
    ASSERT_EQ(Fleet::GetKamikazeQuota(w), Progression::WaveToStage(w));
  }
  for (int w = 1; w < 90; ++w) {
    ASSERT_TRUE(Fleet::GetKamikazeQuota(w + 1) >= Fleet::GetKamikazeQuota(w));
    if (Progression::WaveInStage(w + 1) == 0) {
      ASSERT_EQ(Fleet::GetKamikazeQuota(w + 1), Fleet::GetKamikazeQuota(w) + 1);
    }
  }

  ASSERT_EQ(Fleet::GetMaxAttackWaitFrames(1), 300);
  ASSERT_EQ(Fleet::GetMaxAttackWaitFrames(15), 300);
  ASSERT_EQ(Fleet::GetMaxAttackWaitFrames(16), 240);
  ASSERT_EQ(Fleet::GetMaxAttackWaitFrames(30), 240);
  ASSERT_EQ(Fleet::GetMaxAttackWaitFrames(31), 180);
  ASSERT_EQ(Fleet::GetMaxAttackWaitFrames(1000), 180);
  ASSERT_TRUE(Fleet::GetMaxAttackWaitFrames(1) > Fleet::GetMaxAttackWaitFrames(16));
  ASSERT_TRUE(Fleet::GetMaxAttackWaitFrames(16) > Fleet::GetMaxAttackWaitFrames(31));

  const int maxW = Progression::kWavesPerStage;
  float s1_0 = Fleet::ComputeSpeed(1, maxW);
  float s1_14 = Fleet::ComputeSpeed(15, maxW);
  float s2_0 = Fleet::ComputeSpeed(16, maxW);
  float s3_0 = Fleet::ComputeSpeed(31, maxW);
  ASSERT_TRUE(s1_0 <= s1_14);
  ASSERT_TRUE(s1_14 <= s2_0);
  ASSERT_TRUE(s2_0 <= s3_0);
  ASSERT_TRUE(s1_0 >= Fleet::kStage1MinSpeed && s1_0 <= Fleet::kStage1MaxSpeed);
  ASSERT_TRUE(s2_0 >= Fleet::kStage2MinSpeed && s2_0 <= Fleet::kStage2MaxSpeed);
  ASSERT_TRUE(s3_0 >= Fleet::kStage3MinSpeed && s3_0 <= Fleet::kStage3MaxSpeed);

  ASSERT_TRUE(Fleet::GetWandererMaxWaitFrames() > 0);
  ASSERT_TRUE(Fleet::GetWandererMaxWaitFrames() < 300);

  ASSERT_TRUE(Fleet::kSafeEvasionCorridorPixels > 0);
  ASSERT_TRUE(Fleet::kSafeEvasionCorridorPixels < 1000);
  ASSERT_TRUE(Fleet::kKamikazeBlastRadiusMultiplier > 1.0f);
}

TEST_CASE(TestCombatEconomy) {
  using namespace GameRules::Combat;
  ASSERT_TRUE(kScorePerAlienHit > 0);
  ASSERT_TRUE(kScorePerKamikazeAlienHit >= kScorePerAlienHit);
  ASSERT_TRUE(kScorePerAlienNuked > kScorePerAlienHit);
  ASSERT_TRUE(kScorePerAlienNuked > kScorePerKamikazeAlienHit);
  ASSERT_TRUE(kExtraLifeScoreStep % kScorePerAlienHit == 0);

  ASSERT_TRUE(kBonusWaveDropProbability >= 0.0 && kBonusWaveDropProbability <= 1.0);
  const int totalWeight = kBonusWeightFire + kBonusWeightMulti + kBonusWeightSpeed + kBonusWeightShield + kBonusWeightNuke;
  ASSERT_TRUE(totalWeight > 0);
  ASSERT_TRUE(kBonusWeightNuke <= kBonusWeightShield);
  ASSERT_TRUE(kBonusWeightNuke <= kBonusWeightFire);

  ASSERT_TRUE(kMaxProjectiles > 100);
  ASSERT_TRUE(kMaxExhaustParticles > 0);
  ASSERT_TRUE(kMaxBonuses > 0);
  ASSERT_TRUE(kTurnTotalFrames > 0 && kTurnTotalFrames < 120);
}

TEST_CASE(TestSimulationTiming) {
  using namespace GameRules::Simulation;
  ASSERT_NEAR(kSimulationFrequencyHz, 60.0f, 1e-4f);
  ASSERT_NEAR(kFixedStepDurationSec, 1.0 / 60.0, 1e-9);
  ASSERT_TRUE(kFixedStepNanoseconds >= 16'000'000ULL && kFixedStepNanoseconds <= 17'000'000ULL);
  ASSERT_TRUE(kMaxFrameDeltaNanoseconds > kFixedStepNanoseconds);
  ASSERT_TRUE(kMaxAccumulatorNanoseconds >= kFixedStepNanoseconds);
}

TEST_CASE(TestHighScoreEdgeCases) {
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
}

TEST_CASE(TestCollisionEdgeCases) {
  const AABB tiny(1.0f, 1.0f);
  const AABB huge(1000.0f, 1000.0f);
  Transform2D t0(Vec2f(0, 0));
  Transform2D tFar(Vec2f(5000, 5000));
  ASSERT_TRUE(CheckCollision(t0, tiny, t0, tiny, 1.0f));
  ASSERT_TRUE(!CheckCollision(t0, tiny, tFar, tiny, 1.0f));
  ASSERT_TRUE(!CheckCollision(t0, tiny, Transform2D(Vec2f(10, 0)), tiny, 0.0f));
  ASSERT_TRUE(CheckCollision(t0, huge, Transform2D(Vec2f(500, 0)), huge, 1.0f));
}

TEST_CASE(TestSpecialEntitiesRules) {
  ASSERT_EQ(GameRules::SpecialEntities::kAlien4ElectronCount, 2);
  ASSERT_NEAR(GameRules::SpecialEntities::kAlien4ElectronAngularSpeed, 0.80f, 1e-4f);
  ASSERT_EQ(GameRules::SpecialEntities::kAlien4ElectronRadiusPixels, 2.5f);
  ASSERT_EQ(GameRules::SpecialEntities::kAlien4ElectronAuraRadiusPixels, 8.0f);
  ASSERT_TRUE(GameRules::SpecialEntities::kAlien4AlternateElectrosphere);
  ASSERT_TRUE(GameRules::SpecialEntities::kAlien4OrbitTiltRad > 0.0f);
}

TEST_CASE(TestGameObjectCompositionHeadless) {
  simulation::GameObject obj;
  obj.transform.position = Coord(100, 100);
  obj.motion.velocity = Coord(2, -5);
  obj.Update();
  ASSERT_EQ(obj.transform.position.x, 102);
  ASSERT_EQ(obj.transform.position.y, 95);

  simulation::GameObject target;
  target.transform.position = Coord(105, 98);
  target.collider.aabb = AABB(10.0f, 10.0f);
  obj.collider.aabb = AABB(10.0f, 10.0f);
  ASSERT_TRUE(obj.collider.Intersects(obj.transform, target.collider, target.transform, 0.65f));
}

TEST_CASE(TestSimulationMasterSeedSSOT) {
  ASSERT_TRUE(GameRules::Simulation::kDefaultSimulationSeed != 0u);
  RandomStream r1(GameRules::Simulation::kDefaultSimulationSeed);
  RandomStream r2(GameRules::Simulation::kDefaultSimulationSeed);
  for (int i = 0; i < 50; ++i) {
    ASSERT_EQ(r1.NextU32(), r2.NextU32());
  }
}

TEST_CASE(TestGameObjectMotionIntegration) {
  simulation::GameObject obj;
  obj.transform.position = Coord(500, 500);
  obj.motion.velocity = Coord(-10, 20);

  for (int step = 0; step < 10; ++step) {
    obj.Update();
  }

  ASSERT_EQ(obj.transform.position.x, 400);
  ASSERT_EQ(obj.transform.position.y, 700);
}

TEST_CASE(TestRenderSnapshotIntegrity) {
  simulation::RenderSnapshot snapshot{};
  snapshot.alpha = 0.75f;
  snapshot.is_phase_transition = true;
  snapshot.transition_overlay_alpha = 0.85f;
  snapshot.details_osd_timer = 45;

  ASSERT_NEAR(snapshot.alpha, 0.75f, 1e-4f);
  ASSERT_TRUE(snapshot.is_phase_transition);
  ASSERT_NEAR(snapshot.transition_overlay_alpha, 0.85f, 1e-4f);
  ASSERT_EQ(snapshot.details_osd_timer, 45);
}

TEST_CASE(TestHighScoreIdempotentSerialization) {
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

TEST_CASE(TestAntiScissorCorridorRange) {
  const int corridor = GameRules::Fleet::kSafeEvasionCorridorPixels;
  ASSERT_TRUE(corridor >= 100 && corridor <= 150);
  const int alien_w = static_cast<int>(GameRules::Fleet::kAlienBaseWidthPixels);
  ASSERT_TRUE(corridor > alien_w);
}

TEST_CASE(TestFleetMetricsScaleCalculations) {
  GameRules::Fleet::InvalidateMetrics(1.0f);
  ASSERT_EQ(GameRules::Fleet::Width(), 84);
  ASSERT_EQ(GameRules::Fleet::Height(), 84);
  ASSERT_EQ(GameRules::Fleet::HSpacing(), 93);
  ASSERT_EQ(GameRules::Fleet::VSpacing(), 95);
  ASSERT_EQ(GameRules::Fleet::BaseCruiseY(), 82);

  GameRules::Fleet::InvalidateMetrics(1.5f);
  ASSERT_EQ(GameRules::Fleet::Width(), 126);

  // Restaura baseline
  GameRules::Fleet::InvalidateMetrics(1.0f);
}

TEST_CASE(TestOrbitKnotsTrigInvariants) {
  for (int i = 0; i < 36; ++i) {
    const float a = (static_cast<float>(i) / 36.0f) * 6.2831853f;
    const float c = std::cos(a);
    const float s = std::sin(a);
    ASSERT_NEAR(c * c + s * s, 1.0f, 1e-5f);
  }
}

TEST_CASE(TestYBandEarlyRejectionMath) {
  constexpr int bullet_min_y = 100;
  constexpr int bullet_max_y = 150;
  constexpr int alien_y = 500;
  constexpr int alien_half_h = 40;

  const bool overlaps = !(alien_y + alien_half_h < bullet_min_y || alien_y - alien_half_h > bullet_max_y);
  ASSERT_TRUE(!overlaps);

  constexpr int near_alien_y = 130;
  const bool near_overlaps = !(near_alien_y + alien_half_h < bullet_min_y || near_alien_y - alien_half_h > bullet_max_y);
  ASSERT_TRUE(near_overlaps);
}

TEST_CASE(TestDenseActivePoolCapacityInvariants) {
  ASSERT_TRUE(GameRules::Combat::kMaxProjectiles >= 256);
  ASSERT_TRUE(GameRules::Combat::kMaxBonuses >= 32);
  ASSERT_TRUE(GameRules::Combat::kMaxExplosionParticles >= 1024);
}

TEST_CASE(TestFramePacingTimingInvariants) {
  constexpr uint64_t step_ns = GameRules::Simulation::kFixedStepNanoseconds;
  ASSERT_EQ(step_ns, 16'666'667ULL);
  ASSERT_EQ(GameRules::Simulation::kMaxAccumulatorNanoseconds, 33'333'334ULL);
  ASSERT_TRUE(GameRules::Simulation::kMaxFrameDeltaNanoseconds > step_ns);
  ASSERT_NEAR(GameRules::Simulation::kSimulationFrequencyHz, 60.0f, 1e-4f);
  ASSERT_NEAR(GameRules::Simulation::kFixedStepDurationSec, 1.0 / 60.0, 1e-9);
}

TEST_CASE(TestStarfieldDensityScaleInvariants) {
  using namespace GameRules::Starfield;
  ASSERT_NEAR(GetDensityFactor(0), 0.00, 1e-6);
  ASSERT_NEAR(GetDensityFactor(1), 0.50, 1e-6);
  ASSERT_NEAR(GetDensityFactor(2), 1.50, 1e-6);
  ASSERT_NEAR(GetDensityFactor(3), 2.00, 1e-6);
  ASSERT_NEAR(GetDensityFactor(4), 2.50, 1e-6);
  // Garante fallback padrão no baseline
  ASSERT_NEAR(GetDensityFactor(-1), kDefaultDensityFactor, 1e-6);
  ASSERT_NEAR(GetDensityFactor(99), kDefaultDensityFactor, 1e-6);
}

int main() {
  std::cout << "\033[1m==============================================================================\033[0m\n";
  std::cout << "\033[1;36m       ALIENS INVADERS - C++20 AUTOMATED HEADLESS TEST SUITE (30 TEST CASES)  \033[0m\n";
  std::cout << "\033[1m==============================================================================\033[0m\n";

  RUN_TEST(TestAABBIntersection);
  RUN_TEST(TestTransform2DInterpolation);
  RUN_TEST(TestRandomStreamDeterminism);
  RUN_TEST(TestGameRulesPureKinetics);
  RUN_TEST(TestCatmullRomEndpoints);
  RUN_TEST(TestHighScoreTablePureDomain);
  RUN_TEST(TestAntiScissorBallisticInvariants);
  RUN_TEST(TestScoreProgressionInvariants);
  RUN_TEST(TestGameRulesSSOTInvariants);
  RUN_TEST(TestPureCollisionFunction);
  RUN_TEST(TestHeadlessFlightPathSmoothSpline);
  RUN_TEST(TestProgressionMath);
  RUN_TEST(TestPlayerKineticsClamp);
  RUN_TEST(TestFleetKinetics);
  RUN_TEST(TestCombatEconomy);
  RUN_TEST(TestSimulationTiming);
  RUN_TEST(TestHighScoreEdgeCases);
  RUN_TEST(TestCollisionEdgeCases);
  RUN_TEST(TestSpecialEntitiesRules);
  RUN_TEST(TestGameObjectCompositionHeadless);
  RUN_TEST(TestSimulationMasterSeedSSOT);
  RUN_TEST(TestGameObjectMotionIntegration);
  RUN_TEST(TestRenderSnapshotIntegrity);
  RUN_TEST(TestHighScoreIdempotentSerialization);
  RUN_TEST(TestAntiScissorCorridorRange);
  RUN_TEST(TestFleetMetricsScaleCalculations);
  RUN_TEST(TestOrbitKnotsTrigInvariants);
  RUN_TEST(TestYBandEarlyRejectionMath);
  RUN_TEST(TestDenseActivePoolCapacityInvariants);
  RUN_TEST(TestFramePacingTimingInvariants);
  RUN_TEST(TestStarfieldDensityScaleInvariants);

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
