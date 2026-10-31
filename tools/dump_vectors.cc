// dump_vectors -- golden vectors of the pure simulation kernel for the Rust
// strangler port (RUST.md, Phase 1). No SDL, no side effects beyond stdout:
// every line printed here is a cross-language contract. Floats are emitted as
// raw IEEE-754 bit patterns so last-bit divergences can never hide.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "constants.h"
#include "formation_grid.h"
#include "frame_pacing.h"
#include "highscore_table.h"
#include "math_types.h"
#include "random_stream.h"
#include "score_hash.h"
#include "stage_catalog.h"
#include "trajectory.h"

namespace {

using GameRules::Fleet::FormationGrid;

[[nodiscard]] std::uint32_t FloatBits(float v) noexcept {
  std::uint32_t u = 0;
  std::memcpy(&u, &v, sizeof(u));
  return u;
}

void DumpRng() {
  {
    RandomStream rng(12345u);
    for (int i = 0; i < 64; ++i) {
      std::printf("rng.u32 %d %u\n", i, rng.NextU32());
    }
  }
  // One sequential stream: the draw ORDER below is part of the contract.
  {
    RandomStream rng(999u);
    for (int i = 0; i < 32; ++i) {
      std::printf("rng.uniform_int %d %d\n", i, rng.UniformInt(-50, 49));
    }
    for (int i = 0; i < 16; ++i) {
      std::printf("rng.uniform_float %d %08x\n", i, FloatBits(rng.UniformFloat(0.0f, 1.0f)));
    }
    for (int i = 0; i < 16; ++i) {
      std::printf("rng.bernoulli %d %d\n", i, rng.Bernoulli(0.30f) ? 1 : 0);
    }
    for (int i = 0; i < 32; ++i) {
      std::printf("rng.chance %d %d\n", i, rng.Chance(1, 140) ? 1 : 0);
    }
  }
  {
    RandomStream rng(0u);  // seed-0 remap contract
    std::printf("rng.seed0_first %u\n", rng.NextU32());
  }
}

void DumpMath() {
  const float rounds[] = {-2.5f, -1.5f, -0.5f, -0.4999f, 0.0f, 0.4999f,
                          0.5f,  1.5f,  2.5f,  -3.14f,   3.14f};
  for (const float v : rounds) {
    std::printf("math.fast_round %08x %d\n", FloatBits(v), FastRound(v));
  }
  const int pairs[][2] = {{-7, 2}, {7, 2}, {-7, -2}, {7, -2}, {0, 5},
                          {-1, 1}, {10, 3}, {-10, 3}, {-11, 5}};
  for (const auto& p : pairs) {
    std::printf("math.floor_div %d %d %d\n", p[0], p[1], FloorDiv(p[0], p[1]));
  }
  const int quads[][4] = {{0, 0, 3, 4}, {-2, -2, 2, 2}, {5, 1, 1, 9}, {0, 0, 0, 0}};
  for (const auto& q : quads) {
    std::printf("math.chebyshev %d %d %d %d %d\n", q[0], q[1], q[2], q[3],
                ChebyshevDistance(q[0], q[1], q[2], q[3]));
  }
}

void DumpFormation() {
  for (std::size_t m = 0; m < static_cast<std::size_t>(FormationGrid::kNumStageModes); ++m) {
    for (std::size_t r = 0; r < static_cast<std::size_t>(FormationGrid::kMaxGridRows); ++r) {
      for (std::size_t c = 0; c < static_cast<std::size_t>(FormationGrid::kMaxGridCols); ++c) {
        const auto& u = FormationGrid::kUnitGrid[m][r][c];
        std::printf("unit_grid %d %d %d %08x %08x\n", static_cast<int>(m),
                    static_cast<int>(r), static_cast<int>(c),
                    FloatBits(u.offset_x), FloatBits(u.offset_y));
      }
    }
  }
  const float scales[] = {1.0f, 1.75f, 0.5f};
  for (std::size_t s = 0; s < 3; ++s) {
    FormationGrid::Recompute(scales[s]);
    std::printf("slots.metrics %d %d %d %d %d %d\n", static_cast<int>(s),
                FormationGrid::Width(), FormationGrid::Height(),
                FormationGrid::SpacingX(), FormationGrid::SpacingY(),
                FormationGrid::BaseCruiseY());
    for (std::size_t m = 0; m < static_cast<std::size_t>(FormationGrid::kNumStageModes); ++m) {
      for (std::size_t r = 0; r < static_cast<std::size_t>(FormationGrid::kMaxGridRows); ++r) {
        for (std::size_t c = 0; c < static_cast<std::size_t>(FormationGrid::kMaxGridCols); ++c) {
          const auto& slot = FormationGrid::GetSlot(static_cast<int>(m) + 1,
                                                    static_cast<int>(r), static_cast<int>(c));
          std::printf("slots %d %d %d %d %d %d\n", static_cast<int>(s),
                      static_cast<int>(m), static_cast<int>(r), static_cast<int>(c),
                      slot.offset_x, slot.offset_y);
        }
      }
    }
  }
  // Clamp edges (state: last Recompute above, scale 0.5).
  const auto& lo = FormationGrid::GetSlot(0, -5, -5);
  std::printf("slot_edge low %d %d\n", lo.offset_x, lo.offset_y);
  const auto& hi = FormationGrid::GetSlot(999, 999, 999);
  std::printf("slot_edge high %d %d\n", hi.offset_x, hi.offset_y);
}

void DumpCatalog() {
  for (int w = 1; w <= 75; ++w) {
    const auto p = GameRules::StageCatalog::GetProfile(w);
    std::printf("profile %d %d %d %d %08x %08x %d %d %d %d %d\n", w, p.stage_cycle,
                p.convoy_count, p.max_bombs, FloatBits(p.cycle_bomb_mult),
                FloatBits(p.fleet_speed), p.max_attack_wait_frames, p.kamikaze_quota,
                p.is_radioactive_wave ? 1 : 0, p.is_relativistic_wave ? 1 : 0,
                p.wanderers_allowed ? 1 : 0);
  }
  for (int lvl = 1; lvl <= 75; ++lvl) {
    std::printf("fleet.speed %d %08x\n", lvl,
                FloatBits(GameRules::Fleet::ComputeSpeed(lvl, 15)));
    std::printf("fleet.attack_wait %d %d\n", lvl,
                GameRules::Fleet::GetMaxAttackWaitFrames(lvl));
    std::printf("fleet.kamikaze_quota %d %d\n", lvl,
                GameRules::Fleet::GetKamikazeQuota(lvl));
  }
  std::printf("fleet.wanderer_wait %d\n", GameRules::Fleet::GetWandererMaxWaitFrames());
  for (int s = 0; s <= 6; ++s) {
    std::printf("player.speed %d %08x\n", s, FloatBits(GameRules::Player::ComputeSpeed(s)));
  }
  for (int f = 0; f <= 3; ++f) {
    std::printf("player.fire_interval %d %d\n", f, GameRules::Player::ComputeFireInterval(f));
  }
}

void DumpPacing() {
  const std::uint64_t deltas[] = {16'666'667ULL, 16'666'667ULL, 50'000'000ULL,
                                  300'000'000ULL, 8'000'000ULL, 20'000'000ULL,
                                  16'000'000ULL, 17'000'000ULL, 49'999'999ULL};
  std::uint64_t acc = 0;
  for (std::size_t i = 0; i < 9; ++i) {
    const std::uint64_t raw = deltas[i];
    acc = Pacing::AccumulateBounded(acc, raw);
    std::printf("pacing.step %d %llu %d %llu %llu\n", static_cast<int>(i),
                static_cast<unsigned long long>(raw), Pacing::IsHitch(raw) ? 1 : 0,
                static_cast<unsigned long long>(Pacing::SimulatedDelta(raw)),
                static_cast<unsigned long long>(acc));
  }
  for (int steps = 0; steps <= 2; ++steps) {
    std::printf("pacing.threshold %d %llu\n", steps,
                static_cast<unsigned long long>(Pacing::StepThreshold(steps)));
  }
  const std::uint64_t accs[] = {0ULL, 4'000'000ULL, 8'333'333ULL, 16'666'666ULL};
  for (const std::uint64_t a : accs) {
    std::printf("pacing.alpha %llu %08x\n", static_cast<unsigned long long>(a),
                FloatBits(Pacing::InterpolationAlpha(a)));
  }
  const std::uint64_t consume_in[] = {0ULL, 5ULL, 16'666'666ULL, 16'666'667ULL, 40'000'000ULL};
  for (const std::uint64_t a : consume_in) {
    std::printf("pacing.consume %llu %llu\n", static_cast<unsigned long long>(a),
                static_cast<unsigned long long>(Pacing::ConsumeStep(a)));
  }
}

void DumpScoreHash() {
  const char* payloads[] = {"4;56;26330;3840;2160;60;1792867111;Claudio", "",
                            "hello", "STAGE;WAVE;SCORE"};
  for (std::size_t i = 0; i < 4; ++i) {
    std::printf("score_hash %d %s\n", static_cast<int>(i),
                ScoreHash::LineDigest(payloads[i]).c_str());
  }
}

void DumpHighscore() {
  HighScoreTable table;
  table.Add(HighScore(26330ULL, "Claudio", 1792867111, Coord(3840, 2160), 60, 4, 56));
  table.Add(HighScore(2520ULL, "Claudio", 1792933633, Coord(3840, 2160), 60, 1, 6));
  table.Add(HighScore(670ULL, "Ripley", 1792872474, Coord(1280, 720), 60, 1, 2));
  std::ostringstream os;
  table.Serialize(os);
  const std::string out = os.str();
  std::size_t pos = 0;
  while (pos < out.size()) {
    std::size_t nl = out.find('\n', pos);
    if (nl == std::string::npos) nl = out.size();
    const std::string line = out.substr(pos, nl - pos);
    pos = nl + 1;
    if (!line.empty()) std::printf("serialize %s\n", line.c_str());
  }
}

void DumpSplines() {
  // Fixed literal control sets: arithmetic + sqrt only, so these sections are
  // bit-exact on every platform (no transcendentals, no libm variance).
  const std::vector<std::vector<Vec2f>> cases = {
      {{100.0f, -50.0f}, {260.0f, 120.0f}, {410.0f, 90.0f}, {620.0f, 300.0f}, {700.0f, 900.0f}},
      {{0.0f, 0.0f}, {100.0f, 100.0f}},
      {{512.0f, -40.0f}, {540.0f, 300.0f}, {512.0f, 640.0f}, {512.0f, 780.0f}},
  };
  for (std::size_t s = 0; s < cases.size(); ++s) {
    const std::vector<Vec2f> out = FlightPath::GenerateSmoothSplineF(cases[s]);
    std::printf("spline.meta %d %zu\n", static_cast<int>(s), out.size());
    for (std::size_t j = 0; j < out.size(); ++j) {
      std::printf("spline.pt %d %zu %08x %08x\n", static_cast<int>(s), j,
                  FloatBits(out[j].x), FloatBits(out[j].y));
    }
  }
}

void DumpTrajectories() {
  // Explicit scale-1 formation: the trajectory reads FormationGrid globals and
  // the dumper must never depend on whatever a previous section left behind.
  GameRules::Fleet::FormationGrid::Recompute(1.0f);
  ViewportMetrics metrics{1280, 720, 1.0f};
  const Coord base_cruise(300, 82);
  const FlightPath arrival = FlightPath::Loop(120.0f, -60.0f, 2);

  std::printf("traj.path_count %zu\n", arrival.Size());
  for (std::size_t j = 0; j < arrival.Size(); ++j) {
    std::printf("traj.path %zu %08x %08x\n", j, FloatBits(arrival[j].x), FloatBits(arrival[j].y));
  }

  struct Case {
    int id;
    bool mirrored;
    int col;
    int row;
    unsigned seed;
    int cycle;
    int mode;  // 0 = arrival/docking, 1 = BuildAttack dive, 2 = kamikaze dive
  };
  const Case cases[] = {
      {0, false, 3, 2, 777u, 1, 0},
      {1, true, 5, 3, 778u, 2, 0},
      {2, false, 4, 1, 999u, 1, 1},
      {3, false, 2, 4, 4242u, 3, 2},
  };
  for (const Case& c : cases) {
    Trajectory t(&arrival, c.mirrored, base_cruise, &metrics, c.col, c.row,
                 c.seed, c.cycle, 8);
    const Coord init = t.InitPosition();
    std::printf("traj.init %d %08x %08x\n", c.id,
                FloatBits(static_cast<float>(init.x)),
                FloatBits(static_cast<float>(init.y)));
    if (c.mode == 1) {
      t.BuildAttack(Coord(640, 200));
      std::printf("traj.attack_pts %d %zu\n", c.id, t.AttackPointCount());
    } else if (c.mode == 2) {
      t.BuildKamikazeDive(Coord(300, 150), Coord(640, 640));
      std::printf("traj.attack_pts %d %zu\n", c.id, t.AttackPointCount());
    }
    float x = static_cast<float>(init.x);
    float y = static_cast<float>(init.y);
    const int max_frames = (c.mode == 0) ? 420 : 1500;
    for (int f = 0; f < max_frames; ++f) {
      float nx = x;
      float ny = y;
      t.NextPositionF(x, y, 6.0f, nx, ny);
      x = nx;
      y = ny;
      std::printf("traj.frame %d %d %d %08x %08x\n", c.id, f,
                  static_cast<int>(t.Stage()), FloatBits(x), FloatBits(y));
      if (c.mode != 0 && t.Stage() != Trajectory::attacking) break;
    }
  }
}

}  // namespace

int main() {
  std::printf("# aliens-invaders golden vectors v1 -- pure kernel contract (RUST.md)\n");
  std::printf("# regenerate: cmake --build build --target dump_vectors && ./build/dump_vectors > golden/vectors-cpp.txt\n");
  DumpRng();
  DumpMath();
  DumpFormation();
  DumpCatalog();
  DumpPacing();
  DumpScoreHash();
  DumpHighscore();
  DumpSplines();
  DumpTrajectories();
  return 0;
}
