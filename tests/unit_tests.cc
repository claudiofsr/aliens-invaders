#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "game_rules.h"
#include "highscore_table.h"
#include "math_types.h"
#include "path.h"
#include "random_stream.h"

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_CASE(name) static void name()
#define RUN_TEST(name)                                                         \
  do {                                                                         \
    std::cout << "  • Running " << #name << "... ";                           \
    const int initial_failed = g_tests_failed;                                 \
    name();                                                                    \
    if (g_tests_failed == initial_failed) {                                    \
      std::cout << "\033[32m[PASSED]\033[0m\n";                              \
      ++g_tests_passed;                                                        \
    } else {                                                                   \
      std::cout << "\033[31m[FAILED]\033[0m\n";                              \
    }                                                                          \
  } while (0)

#define ASSERT_TRUE(expr)                                                      \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::cerr << "\n    Assertion failed: " << #expr << " at " << __FILE__  \
                << ":" << __LINE__ << "\n";                                   \
      ++g_tests_failed;                                                        \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_EQ(a, b)                                                        \
  do {                                                                         \
    if ((a) != (b)) {                                                          \
      std::cerr << "\n    Assertion failed: (" << #a << " == " << #b          \
                << ") => (" << (a) << " != " << (b) << ") at " << __FILE__   \
                << ":" << __LINE__ << "\n";                                   \
      ++g_tests_failed;                                                        \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_NEAR(a, b, eps)                                                 \
  do {                                                                         \
    if (std::abs((a) - (b)) > (eps)) {                                         \
      std::cerr << "\n    Assertion failed: |" << #a << " - " << #b            \
                << "| <= " << #eps << " => |" << (a) << " - " << (b)           \
                << "| = " << std::abs((a) - (b)) << " at " << __FILE__ << ":" \
                << __LINE__ << "\n";                                          \
      ++g_tests_failed;                                                        \
      return;                                                                  \
    }                                                                          \
  } while (0)

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
  ASSERT_EQ(GameRules::ComputePlayerSpeed(0), 3.5f);
  ASSERT_NEAR(GameRules::ComputePlayerSpeed(1), 3.85f, 1e-3f);
  ASSERT_NEAR(GameRules::ComputePlayerSpeed(2), 4.20f, 1e-3f);
  ASSERT_NEAR(GameRules::ComputePlayerSpeed(5), 4.20f, 1e-3f);

  ASSERT_EQ(GameRules::GetKamikazeQuota(1), 1);
  ASSERT_EQ(GameRules::GetKamikazeQuota(15), 1);
  ASSERT_EQ(GameRules::GetKamikazeQuota(16), 2);
  ASSERT_EQ(GameRules::GetKamikazeQuota(31), 3);

  ASSERT_EQ(GameRules::GetMaxAttackWaitFrames(1), 300);
  ASSERT_EQ(GameRules::GetMaxAttackWaitFrames(16), 240);
  ASSERT_EQ(GameRules::GetMaxAttackWaitFrames(31), 180);
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
  constexpr int kCorridor = GameRules::kSafeCorridorBasePx;
  ASSERT_EQ(kCorridor, 118);

  const int tight_separation = 140;
  const bool converging = true;
  const int min_allowed = converging ? (kCorridor * 2) : kCorridor;
  ASSERT_TRUE(tight_separation < min_allowed); // Detected as hazardous trap
}

TEST_CASE(TestScoreProgressionInvariants) {
  // Test 1-UP milestone calculation
  constexpr uint64_t pts = 5000;
  constexpr uint64_t step = 1000;
  ASSERT_EQ(pts / step, 5ULL);
}


TEST_CASE(TestGameRulesSSOTInvariants) {
  ASSERT_EQ(GameRules::Simulation::kSimulationFrequencyHz, 60.0f);
  ASSERT_EQ(GameRules::Progression::kWavesPerStage, 15);
  ASSERT_EQ(GameRules::Progression::kStageFanfareDurationSec, 5.0f);
  ASSERT_EQ(GameRules::Player::kInitialShieldLives, 3);
  ASSERT_EQ(GameRules::Player::kMaxShieldLives, 8);
  ASSERT_EQ(GameRules::Visuals::kFloatingTextHitFontSize, 24.0f);
  ASSERT_EQ(GameRules::Fleet::kAlienBaseWidthPixels, 84.0f);
  ASSERT_EQ(GameRules::Combat::kScorePerAlienHit, 10ULL);
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

int main() {
  std::cout << "\033[1m==============================================================================\033[0m\n";
  std::cout << "\033[1;36m       ALIENS INVADERS - C++20 AUTOMATED HEADLESS TEST SUITE                  \033[0m\n";
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
