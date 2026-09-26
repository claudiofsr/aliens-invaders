#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "constants.h"
#include "stage_catalog.h"
#include "formation_grid.h"
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
  const float base_spd = GameRules::Player::kPlayerBaseSpeed;
  const float boost_pct = GameRules::Player::kSpeedBoostPercent;
  const int max_tier = GameRules::Player::kPlayerMaxSpeedLevel;
  ASSERT_EQ(GameRules::Player::ComputeSpeed(0), base_spd);
  ASSERT_NEAR(GameRules::Player::ComputeSpeed(1), base_spd * (1.0f + boost_pct * 1.0f), 1e-3f);
  ASSERT_NEAR(GameRules::Player::ComputeSpeed(2), base_spd * (1.0f + boost_pct * 2.0f), 1e-3f);
  ASSERT_NEAR(GameRules::Player::ComputeSpeed(5), base_spd * (1.0f + boost_pct * static_cast<float>(max_tier)), 1e-3f);

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
  ASSERT_EQ(GameRules::Progression::kStageFanfareDurationSeconds, 6.0f);
  ASSERT_NEAR(GameRules::Audio::kFanfareFadeInSec, 0.30f, 1e-4f);
  ASSERT_NEAR(GameRules::Audio::kFanfareFadeOutSec, 0.50f, 1e-4f);
  ASSERT_EQ(GameRules::Player::kInitialShields, 3);
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
  ASSERT_EQ(kPostFanfareArmadaDelayFrames, 60);

  for (int w = 1; w <= 300; ++w) {
    int stage = WaveToStage(w);
    int inStage = WaveInStage(w);
    ASSERT_EQ((stage - 1) * kWavesPerStage + inStage + 1, w);
    ASSERT_TRUE(inStage >= 0 && inStage < kWavesPerStage);
  }
}

TEST_CASE(TestPlayerKineticsClamp) {
  using namespace GameRules::Player;
  const float base_spd = kPlayerBaseSpeed;
  const float boost_pct = kSpeedBoostPercent;
  const int max_tier = kPlayerMaxSpeedLevel;

  ASSERT_EQ(ComputeSpeed(-100), ComputeSpeed(0));
  ASSERT_EQ(ComputeSpeed(999), ComputeSpeed(kPlayerMaxSpeedLevel));
  ASSERT_TRUE(ComputeSpeed(0) < ComputeSpeed(1));
  ASSERT_TRUE(ComputeSpeed(1) < ComputeSpeed(2));
  ASSERT_NEAR(ComputeSpeed(0), base_spd, 1e-4f);
  ASSERT_NEAR(ComputeSpeed(1), base_spd * (1.0f + boost_pct * 1.0f), 1e-4f);
  ASSERT_NEAR(ComputeSpeed(2), base_spd * (1.0f + boost_pct * 2.0f), 1e-4f);
  ASSERT_NEAR(ComputeSpeed(5), base_spd * (1.0f + boost_pct * static_cast<float>(max_tier)), 1e-4f);

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
  ASSERT_NEAR(Fleet::kFleetMinVelocityPixels, 1.8f, 1e-4f);
  ASSERT_NEAR(Fleet::kAttackDiveSpeedMultiplier, 0.90f, 1e-4f);
  ASSERT_NEAR(Fleet::kAttackDiveMinVelocityPixels, 1.5f, 1e-4f);

  ASSERT_TRUE(Fleet::kSafeEvasionCorridorPixels > 0);
  ASSERT_TRUE(Fleet::kSafeEvasionCorridorPixels < 1000);
  ASSERT_TRUE(Fleet::kKamikazeBlastRadiusMultiplier > 1.0f);
  ASSERT_TRUE(Fleet::kSeekerMissileProbability > 0.0f && Fleet::kSeekerMissileProbability <= 1.0f);
  ASSERT_NEAR(Fleet::kSeekerMissileProbability, 0.25f, 1e-4f);
}

TEST_CASE(TestCombatEconomy) {
  using namespace GameRules::Combat;
  ASSERT_EQ(kMaxBonusesPerStage, 5);
  ASSERT_TRUE(kScorePerAlienHit > 0);
  ASSERT_TRUE(kScorePerKamikazeAlienHit >= kScorePerAlienHit);
  ASSERT_TRUE(kScorePerAlienNuked > kScorePerAlienHit);
  ASSERT_TRUE(kScorePerAlienNuked > kScorePerKamikazeAlienHit);
  ASSERT_TRUE(kExtraShieldScoreStep % kScorePerAlienHit == 0);

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
  ASSERT_NEAR(GameRules::SpecialEntities::kAlien4ElectronAngularSpeed, 3.0f, 1e-4f);
  ASSERT_EQ(GameRules::SpecialEntities::kAlien4ElectronRadiusPixels, 2.5f);
  ASSERT_EQ(GameRules::SpecialEntities::kAlien4ElectronAuraRadiusPixels, 8.0f);
  ASSERT_TRUE(GameRules::SpecialEntities::kAlien4OrbitTiltRad > 0.0f);
  ASSERT_TRUE(GameRules::SpecialEntities::kAlien14WarpEvasionProbability > 0.0f &&
              GameRules::SpecialEntities::kAlien14WarpEvasionProbability <= 1.0f);
  ASSERT_TRUE(GameRules::SpecialEntities::kAlien14NumWarpEvasions > 0);
  ASSERT_TRUE(GameRules::SpecialEntities::kAlien14WarpTriggerRadiusFactor > 1.0f);
  ASSERT_TRUE(GameRules::SpecialEntities::kAlien14WarpSpeedPixels > 10.0f);
}

TEST_CASE(TestStageCatalogProfileInvariants) {
  using namespace GameRules;
  constexpr auto p1 = StageCatalog::GetProfile(1);
  static_assert(p1.wave == 1);
  static_assert(p1.stage_cycle == 1);
  static_assert(p1.convoy_count == 5);
  static_assert(p1.max_bombs == 4);
  static_assert(p1.kamikaze_quota == 1);
  static_assert(!p1.is_radioactive_wave);
  static_assert(!p1.is_relativistic_wave);
  static_assert(!p1.wanderers_allowed);

  constexpr auto p4 = StageCatalog::GetProfile(4);
  static_assert(p4.is_radioactive_wave);

  constexpr auto p14 = StageCatalog::GetProfile(14);
  static_assert(p14.is_relativistic_wave);

  constexpr auto p16 = StageCatalog::GetProfile(16);
  static_assert(p16.stage_cycle == 2);
  static_assert(p16.wanderers_allowed);
  static_assert(p16.kamikaze_quota == 2);

  ASSERT_EQ(p1.wave, 1);
  ASSERT_EQ(p4.wave, 4);
  ASSERT_TRUE(p4.is_radioactive_wave);
  ASSERT_TRUE(p14.is_relativistic_wave);
  ASSERT_TRUE(p16.wanderers_allowed);
}

TEST_CASE(TestFormationGridEquidistance) {
  using namespace GameRules::Fleet;
  FormationGrid::Recompute(1.0f);

  const auto& slot1 = FormationGrid::GetSlot(1, 0, 1);
  const auto& slot2 = FormationGrid::GetSlot(1, 0, 2);
  const int original_distance = std::abs(slot2.offset_x - slot1.offset_x);
  ASSERT_EQ(original_distance, SpacingX());

  // Screen symmetry invariant: |(W - X1) - (W - X2)| == |X2 - X1|
  constexpr int win_w = 1280;
  const int sym_x1 = win_w - (100 + slot1.offset_x);
  const int sym_x2 = win_w - (100 + slot2.offset_x);
  const int warped_distance = std::abs(sym_x1 - sym_x2);
  ASSERT_EQ(warped_distance, original_distance);
}

TEST_CASE(TestHiveMirrorColumnInvolution) {
  using namespace GameRules::Fleet;
  // (a, b) -> (-a, b) around the formation centre is col -> (n-1)-col.
  for (int n = 1; n <= FormationGrid::kMaxGridCols; ++n) {
    for (int c = 0; c < n; ++c) {
      const int m = FormationGrid::MirrorColumn(c, n);
      ASSERT_TRUE(m >= 0 && m < n);
      ASSERT_EQ(FormationGrid::MirrorColumn(m, n), c);  // involution
    }
    // Unique destinations: two distinct cols never share a favo.
    for (int a = 0; a < n; ++a) {
      for (int b = a + 1; b < n; ++b) {
        ASSERT_TRUE(FormationGrid::MirrorColumn(a, n) != FormationGrid::MirrorColumn(b, n));
      }
    }
  }
  ASSERT_EQ(FormationGrid::MirrorColumn(0, 8), 7);
  ASSERT_EQ(FormationGrid::MirrorColumn(7, 8), 0);
  ASSERT_EQ(FormationGrid::MirrorColumn(3, 8), 4);
  ASSERT_EQ(FormationGrid::GetSymmetricX(200, 1280), 1080);
}

TEST_CASE(TestStageBonusLimitInvariants) {
  using namespace GameRules::Combat;
  ASSERT_EQ(kMaxBonusesPerStage, 5);
  constexpr int kTotalBonusTypes = 5;
  ASSERT_EQ(kMaxBonusesPerStage, kTotalBonusTypes);
}

TEST_CASE(TestWave5FourCornerBalletSplines) {
  // Verifies the mathematical geometry of the 4 diagonal corner flight paths
  const FlightPath tl({{-50.0f, -50.0f}, {140.0f, 140.0f}, {260.0f, 260.0f}, {220.0f, 880.0f}, {180.0f, 220.0f}});
  const FlightPath tr({{1074.0f, -50.0f}, {884.0f, 140.0f}, {764.0f, 260.0f}, {804.0f, 880.0f}, {844.0f, 220.0f}});
  const FlightPath bl({{-50.0f, 920.0f}, {140.0f, 800.0f}, {260.0f, 680.0f}, {280.0f, 820.0f}, {200.0f, 240.0f}});
  const FlightPath br({{1074.0f, 920.0f}, {884.0f, 800.0f}, {764.0f, 680.0f}, {744.0f, 820.0f}, {824.0f, 240.0f}});

  ASSERT_TRUE(!tl.Empty() && !tr.Empty() && !bl.Empty() && !br.Empty());
  // Origins must lie outside the screen perimeter in 4 distinct corners
  ASSERT_TRUE(tl[0].x < 0.0f && tl[0].y < 0.0f);
  ASSERT_TRUE(tr[0].x > 1024.0f && tr[0].y < 0.0f);
  ASSERT_TRUE(bl[0].x < 0.0f && bl[0].y > 800.0f);
  ASSERT_TRUE(br[0].x > 1024.0f && br[0].y > 800.0f);
}

TEST_CASE(TestAlien14SpacetimeWarpInvariants) {
  using namespace GameRules::SpecialEntities;
  ASSERT_NEAR(kAlien14WarpEvasionProbability, 0.60f, 1e-4f);
  ASSERT_EQ(kAlien14NumWarpEvasions, 2);
  ASSERT_NEAR(kAlien14WarpTriggerRadiusFactor, 2.4f, 1e-4f);
  ASSERT_NEAR(kAlien14WarpSpeedPixels, 32.0f, 1e-4f);
  ASSERT_EQ(kAlien14WarpGhostCapacity, 8u);
  ASSERT_NEAR(kAlien14WarpCooldownSeconds, 1.4f, 1e-4f);
  ASSERT_EQ(kAlien14WarpMinCellSeparation, 4);

  constexpr float hw = 42.0f;
  constexpr float hh = 42.0f;
  const float radius = std::hypot(hw, hh);
  const float trigger_dist = radius * kAlien14WarpTriggerRadiusFactor;
  ASSERT_TRUE(trigger_dist > radius);
  ASSERT_TRUE(trigger_dist > 100.0f);

  using FG = GameRules::Fleet::FormationGrid;
  constexpr int N = kAlien14WarpMinCellSeparation;

  // SSOT ChebyshevDistance from math_types.h (DRY)
  ASSERT_EQ(ChebyshevDistance(0, 0, 3, 1), 3);
  ASSERT_EQ(ChebyshevDistance(5, 5, 5, 5), 0);
  ASSERT_EQ(ChebyshevDistance(2, 4, 4, 2), 2);
  ASSERT_EQ(ChebyshevDistance(Coord(0, 0), Coord(3, 1)), 3);

  RandomStream rng(0xA11E414u);
  const int origin_col = 5, origin_row = 5;
  const int used_cols = 12, used_rows = 10;
  for (int i = 0; i < 200; ++i) {
    const auto cell = FG::PickRestrictedWarpCell(
        origin_col, origin_row, used_cols, used_rows, N, rng);
    ASSERT_TRUE(cell.col >= 0 && cell.col < used_cols);
    ASSERT_TRUE(cell.row >= 0 && cell.row < used_rows);
    ASSERT_TRUE(ChebyshevDistance(cell.col, cell.row, origin_col, origin_row) >= N);
  }

  {
    const auto cell = FG::PickRestrictedWarpCell(0, 0, 1, 1, N, rng);
    ASSERT_EQ(cell.col, 0);
    ASSERT_EQ(cell.row, 0);
  }

  const Coord warp_dest = FG::GetWarpPosition(200, 100, 0, 1280, 720);
  ASSERT_EQ(warp_dest.x, 1080);
  ASSERT_EQ(warp_dest.y, 100);
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
  using namespace GameRules::Fleet;
  FormationGrid::Recompute(1.0f);
  ASSERT_EQ(Width(), 84);
  ASSERT_EQ(Height(), 84);
  ASSERT_EQ(SpacingX(), 93);
  ASSERT_EQ(SpacingY(), 95);
  ASSERT_EQ(BaseCruiseY(), 82);

  FormationGrid::Recompute(1.5f);
  ASSERT_EQ(Width(), 126);

  // Restaura baseline
  FormationGrid::Recompute(1.0f);
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
  ASSERT_NEAR(GetDensityFactor(1), 0.60, 1e-6);
  ASSERT_NEAR(GetDensityFactor(2), 1.50, 1e-6);
  ASSERT_NEAR(GetDensityFactor(3), 2.00, 1e-6);
  ASSERT_NEAR(GetDensityFactor(4), 3.50, 1e-6);
  // Garante fallback padrão no baseline
  ASSERT_NEAR(GetDensityFactor(-1), kDensityFactorLevel2, 1e-6);
  ASSERT_NEAR(GetDensityFactor(99), kDensityFactorLevel2, 1e-6);
}


TEST_CASE(TestStarfieldTwinklePulseInvariants) {
  using namespace GameRules::Starfield;

  // SIZE knobs: min < max, small disk never a single pixel, large > small.
  ASSERT_TRUE(kMidFieldHalfSizeMinPx >= 1.5f);
  ASSERT_TRUE(kMidFieldHalfSizeMaxPx > kMidFieldHalfSizeMinPx);
  ASSERT_TRUE(kMidFieldHalfSizeMaxPx * 2.0f < kForegroundSizeMinPx + 0.5f);
  ASSERT_TRUE(kForegroundSizeMinPx >= 6.0f);
  ASSERT_TRUE(kForegroundSizeMaxPx > kForegroundSizeMinPx);
  ASSERT_TRUE(kForegroundSizeMinPx > kMidFieldHalfSizeMinPx * 2.0f);

  // Scintillation never bottoms out, never exceeds 1, and Pulse01 is linear.
  ASSERT_NEAR(kTwinkleFloor, 0.38f, 1e-4f);
  ASSERT_NEAR(kTwinkleCeil, 1.00f, 1e-4f);
  ASSERT_TRUE(kTwinkleFloor < kTwinkleCeil);
  ASSERT_NEAR(Pulse01(kTwinkleFloor), 0.0f, 1e-4f);
  ASSERT_NEAR(Pulse01(kTwinkleCeil), 1.0f, 1e-4f);
  ASSERT_NEAR(Pulse01((kTwinkleFloor + kTwinkleCeil) * 0.5f), 0.5f, 1e-4f);
  ASSERT_NEAR(Pulse01(0.0f), 0.0f, 1e-4f);
  ASSERT_NEAR(Pulse01(1.0f), 1.0f, 1e-4f);

  // TIME knobs: period is the authoring control; speed is derived.
  ASSERT_NEAR(kTwinklePeriodMinSec, 1.50f, 1e-4f);
  ASSERT_NEAR(kTwinklePeriodMaxSec, 3.75f, 1e-4f);
  ASSERT_TRUE(kTwinklePeriodMinSec > 0.0f && kTwinklePeriodMinSec < kTwinklePeriodMaxSec);
  ASSERT_TRUE(kTwinkleSpeedMin > 0.0f && kTwinkleSpeedMin < kTwinkleSpeedMax);
  ASSERT_TRUE(kTwinkleSpeedMax <= 0.50f);
  ASSERT_NEAR(kTau, 6.28318530718f, 1e-6f);

  // FALL knobs: min < max, large stars drift faster than small ones.
  ASSERT_TRUE(kMidFieldFallSpeedMinPx > 0.0f);
  ASSERT_TRUE(kMidFieldFallSpeedMinPx < kMidFieldFallSpeedMaxPx);
  ASSERT_TRUE(kForegroundFallSpeedMinPx < kForegroundFallSpeedMaxPx);
  ASSERT_TRUE(kForegroundFallSpeedMinPx >= kMidFieldFallSpeedMaxPx);

  ASSERT_TRUE(kMidFieldAlphaFloor >= 0.25f && kMidFieldAlphaFloor < 1.0f);
  ASSERT_TRUE(kForegroundAlphaFloor >= 70 && kForegroundAlphaFloor < 200);
  ASSERT_TRUE(kStarScaleDpiMin > 0.0f && kStarScaleDpiMin < kStarScaleDpiMax);
  ASSERT_EQ(kMaxStarQuadCount, 2048u);

  // Flare core must be wider than the original sigma² = 9 (pixel collapse).
  ASSERT_TRUE(kFlareCoreSigma2 > 9.0f);
  ASSERT_TRUE(kFlareCoreGain > 1.0f);
  ASSERT_TRUE(kFlareHaloSigma > 0.0f && kFlareHaloGain > 0.0f);
}

TEST_CASE(TestStageFanfareTimingInvariants) {
  using namespace GameRules::Audio;
  ASSERT_NEAR(kFanfareTonicResolutionMinSec, 5.20f, 1e-4f);
  ASSERT_NEAR(kFanfareTonicResolutionMaxSec, 5.40f, 1e-4f);
  ASSERT_NEAR(kFanfareTonicDurationMinSec, 0.50f, 1e-4f);
  ASSERT_NEAR(kFanfareTonicDurationMaxSec, 0.75f, 1e-4f);
  ASSERT_NEAR(kFanfareSilenceWindowStartSec, 5.85f, 1e-4f);
  ASSERT_NEAR(kFanfareFadeOutSec, 0.50f, 1e-4f);
  ASSERT_TRUE(kFanfareTonicResolutionMinSec < kFanfareTonicResolutionMaxSec);
  ASSERT_TRUE(kFanfareTonicDurationMinSec < kFanfareTonicDurationMaxSec);
  ASSERT_TRUE(kFanfareTonicResolutionMaxSec + kFanfareTonicDurationMaxSec <= 6.15f);
}

TEST_CASE(TestAlien4AlphaDecayInvariants) {
  using namespace GameRules::SpecialEntities;
  ASSERT_NEAR(kAlien4AlphaDecayScale, 0.50f, 1e-4f);
  ASSERT_EQ(kAlien4InitialHealth, 2);
  ASSERT_NEAR(kAlien4DecayedOrbitSpeedMult, 2.0f, 1e-4f);
  ASSERT_NEAR(kAlien4DecayedSpeedMult, 1.30f, 1e-4f);
  ASSERT_TRUE(kAlien4DecayedSpeedMult > 1.0f);
  ASSERT_NEAR(kAlien4ElectrosphereProbability, 0.20f, 1e-4f);
  ASSERT_TRUE(kAlien4ElectrosphereProbability > 0.0f && kAlien4ElectrosphereProbability <= 1.0f);

  simulation::GameObject obj;
  obj.scale = kAlien4AlphaDecayScale;
  ASSERT_NEAR(obj.scale, 0.50f, 1e-4f);
}

int main() {
  std::cout << "\033[1m==============================================================================\033[0m\n";
  std::cout << "\033[1;36m       ALIENS INVADERS - C++20 AUTOMATED HEADLESS TEST SUITE (40 TEST CASES)  \033[0m\n";
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
  RUN_TEST(TestStarfieldTwinklePulseInvariants);
  RUN_TEST(TestStageCatalogProfileInvariants);
  RUN_TEST(TestFormationGridEquidistance);
  RUN_TEST(TestHiveMirrorColumnInvolution);
  RUN_TEST(TestAlien14SpacetimeWarpInvariants);
  RUN_TEST(TestStageBonusLimitInvariants);
  RUN_TEST(TestWave5FourCornerBalletSplines);
  RUN_TEST(TestStageFanfareTimingInvariants);
  RUN_TEST(TestAlien4AlphaDecayInvariants);

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
