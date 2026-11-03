// dump_vectors -- golden vectors of the pure simulation kernel and of the
// entity layer for the Rust strangler port (RUST.md). No SDL, no side effects
// beyond stdout: every line printed here is a cross-language contract. Floats
// are emitted as raw IEEE-754 bit patterns so last-bit divergences can never
// hide.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "actors.h"
#include "config.h"
#include "constants.h"
#include "explosion.h"
#include "formation_grid.h"
#include "frame_pacing.h"
#include "game_object.h"
#include "highscore_table.h"
#include "level_data.h"
#include "managers.h"
#include "math_types.h"
#include "presentation_sink.h"
#include "random_stream.h"
#include "score.h"
#include "score_hash.h"
#include "stage_catalog.h"
#include "stars.h"
#include "texture_provider.h"
#include "trajectory.h"

namespace {

using GameRules::Fleet::FormationGrid;

[[nodiscard]] std::uint32_t FloatBits(float v) noexcept {
  std::uint32_t u = 0;
  std::memcpy(&u, &v, sizeof(u));
  return u;
}

[[nodiscard]] std::uint64_t DoubleBits(double v) noexcept {
  std::uint64_t u = 0;
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


// ---------------------------------------------------------------------------
// Slice 4 (RUST.md): golden-vector recording of the ENTITY layer.
//
// The dumper links the pure simulation TUs (actors/managers/explosion) and
// supplies the two shell boundaries they consume:
//   * RecordingSink    -- a PresentationSink capturing cosmetic intents as
//                         corpus rows (the Rust port must reproduce the event
//                         stream, not just the kinematics);
//   * HeadlessTextures -- a TextureProvider backed by dimension-only Pix, so
//                         hitboxes resolve without GPU or PNG decoding.
// Gameplay math runs exactly as in the shipped binary; only presentation and
// pixels are stubbed. Row sections:
//   ent.pix     headless geometry contract (dimensions per TextureId)
//   ent.bl.*    BulletsManager micro-replay: straight bullet, relativistic
//               bomb, turning seeker bomb, collision intercept, warp-trigger
//               radius gate, bomb-proximity gate, fair-play trap truth table
//   ent.wp.*    Alien14 relativistic warp: seed-searched birth roll, occupied
//               formation, two jumps plus the exhausted-budget no-op
//   ent.dc.*    Alien4 alpha decay: seed-searched electrosphere, decay trigger
//               (scale 0.5, speed x1.3) and the health-1 no-op
//   ent.kz.*    kamikaze guided dive through the Alien state machine
//               (attacking -> wrap -> re-dock -> cruise)
//   ent.fleet.* full fleet replay in PRODUCTION tick order with a coverage-
//               searched seed (warp used, alpha decay born, bonus collected);
//               the section is self-contained: convoy table, arrival paths
//               and per-frame inputs are all rows
//   ent.pl.*    player movement/fire/bonus/shield economy replay
//   ent.ex.*    explosion particle pool, Clear, screen-wide nova
// ---------------------------------------------------------------------------

constexpr int kEntityWinW = 1280;
constexpr int kEntityWinH = 720;

/// Canonical headless pixmap geometry -- the ent.pix rows are the contract.
/// Real sprites come from the baked PNGs, but the simulation only ever reads
/// Width()/Height(), so a stable dimension table is all the two sides of the
/// port need for bit-identical hitboxes.
constexpr int kHeadlessDims[static_cast<size_t>(TextureId::Count)][2] = {
    {0, 0},    // None
    {48, 32},  // Player
    {48, 32},  // PlayerAlt
    {8, 16},   // Bullet
    {12, 20},  // Bomb
    {28, 28},  // BonusFire
    {28, 28},  // BonusShield
    {28, 28},  // BonusSpeed
    {28, 28},  // BonusMulti
    {28, 28},  // BonusNuke
    {36, 30},  // Alien1
    {36, 30},  // Alien2
    {36, 30},  // Alien3
    {40, 34},  // Alien4
    {36, 30},  // Alien5
    {36, 30},  // Alien6
    {36, 30},  // Alien7
    {36, 30},  // Alien8
    {36, 30},  // Alien9
    {36, 30},  // Alien10
    {36, 30},  // Alien11
    {36, 30},  // Alien12
    {44, 36},  // Alien13
    {40, 34},  // Alien14
    {44, 36},  // Alien15
};

/// TextureProvider for the dumper: pre-builds one dimension-only Pix per id
/// (Pix::CreateHeadless owns no GPU texture; the release hook stays null).
class HeadlessTextures final : public TextureProvider {
  std::array<std::unique_ptr<Pix>, static_cast<size_t>(TextureId::Count)> pixes_{};

 public:
  HeadlessTextures() {
    for (std::size_t i = 0; i < pixes_.size(); ++i) {
      pixes_[i] = Pix::CreateHeadless(
          Coord(kHeadlessDims[i][0], kHeadlessDims[i][1]));
    }
  }
  [[nodiscard]] const Pix* Get(TextureId id) const override {
    return pixes_[static_cast<size_t>(id)].get();
  }
};

/// PresentationSink that records cosmetic intents instead of presenting them.
/// Events accumulate between frames and are emitted by Drain() with a frame
/// stamp, so the corpus interleaves state and events in production order.
class RecordingSink final : public PresentationSink {
 public:
  enum class Kind : std::uint8_t { kText, kFlash, kTrauma, kSfx };
  struct Event {
    Kind kind{Kind::kText};
    int x{0};
    int y{0};
    int r{0};
    int g{0};
    int b{0};
    int i0{0};       // ttl_frames / flash frames / sound id
    float f0{0.0f};  // font size / trauma amount / pan
    std::string text;
  };
  std::vector<Event> events;

  void FloatingText(Coord pos, const char* text, std::uint8_t r, std::uint8_t g,
                    std::uint8_t b, float font_size, int ttl_frames) override {
    events.push_back(Event{Kind::kText, pos.x, pos.y, r, g, b, ttl_frames,
                           font_size, text});
  }
  void Flash(std::uint8_t r, std::uint8_t g, std::uint8_t b,
             int frames) override {
    events.push_back(Event{Kind::kFlash, 0, 0, r, g, b, frames, 0.0f, {}});
  }
  void Trauma(float amount) override {
    events.push_back(Event{Kind::kTrauma, 0, 0, 0, 0, 0, 0, amount, {}});
  }
  void Sfx(int sound_id, float pan) override {
    events.push_back(Event{Kind::kSfx, 0, 0, 0, 0, 0, sound_id, pan, {}});
  }

  /// Emits the pending events as "<prefix>.sink <frame> <kind> ..." rows and
  /// clears the queue. Text payloads occupy the rest of the line (they may
  /// contain spaces and '%' signs -- consumers must read to end of line).
  void Drain(const char* prefix, int frame) {
    for (const Event& e : events) {
      switch (e.kind) {
        case Kind::kText:
          std::printf("%s.sink %d text %d %d %d %d %d %08x %d %s\n", prefix,
                      frame, e.x, e.y, e.r, e.g, e.b, FloatBits(e.f0), e.i0,
                      e.text.c_str());
          break;
        case Kind::kFlash:
          std::printf("%s.sink %d flash %d %d %d %d\n", prefix, frame, e.r,
                      e.g, e.b, e.i0);
          break;
        case Kind::kTrauma:
          std::printf("%s.sink %d trauma %08x\n", prefix, frame,
                      FloatBits(e.f0));
          break;
        case Kind::kSfx:
          std::printf("%s.sink %d sfx %d %08x\n", prefix, frame, e.i0,
                      FloatBits(e.f0));
          break;
      }
    }
    events.clear();
  }
};

/// Deterministic scripted input (numeric-recipes LCG). The same stream drives
/// the fleet seed search and the recorded replay, and every frame row carries
/// its own dir value, so the corpus fully describes the input that a Rust
/// replayer must consume.
struct InputScript {
  std::uint32_t state{0};
  explicit InputScript(std::uint32_t seed) noexcept : state(seed) {}
  void Step() noexcept { state = state * 1664525u + 1013904223u; }
  [[nodiscard]] int Dir() const noexcept {
    return static_cast<int>(state >> 24) % 3 - 1;
  }
};

// Arrival splines shared by the entity scenarios. The loop is the exact same
// construction as DumpTrajectories (traj.path rows) and the two literal
// control sets are the exact sets of DumpSplines cases 0 and 2 (spline.pt
// rows), which keeps the corpus internally consistent. The fleet section
// re-emits all three as ent.fleet.path rows so the replay contract is fully
// self-contained for the Rust side.
[[nodiscard]] const FlightPath& EntityArrivalLoop() {
  static const FlightPath path = FlightPath::Loop(120.0f, -60.0f, 2);
  return path;
}
[[nodiscard]] const FlightPath& EntityArrivalArc() {
  static const FlightPath path{{100.0f, -50.0f},
                               {260.0f, 120.0f},
                               {410.0f, 90.0f},
                               {620.0f, 300.0f},
                               {700.0f, 900.0f}};
  return path;
}
[[nodiscard]] const FlightPath& EntityArrivalSpire() {
  static const FlightPath path{{512.0f, -40.0f},
                               {540.0f, 300.0f},
                               {512.0f, 640.0f},
                               {512.0f, 780.0f}};
  return path;
}

/// ent.pix <texture_id> <base_w> <base_h>
void DumpEntityPix() {
  for (int id = 0; id < static_cast<int>(TextureId::Count); ++id) {
    std::printf("ent.pix %d %d %d\n", id, kHeadlessDims[id][0],
                kHeadlessDims[id][1]);
  }
}

/// BulletsManager micro-replay. Row layouts:
///   ent.bl.add     <i> <x> <y> <spdx> <spdy> <rel> <turning> <trigger_y> <defl_bits>
///   ent.bl.frame   <f> <nb> <exhaust_nb> <min_y> <max_y>
///   ent.bl.slot    <f> <i> <x> <y> <fx_bits> <fy_bits> <spdx> <spdy> <rel>
///                  <radio> <wave_phase> <turning> <turn_done> <turn_timer>
///                  <render_angle_bits> <defl_bits> <speed_mag_bits>
///   ent.bl.exhaust <f> <i> <posx_bits> <posy_bits> <velx_bits> <vely_bits> <life>
///   ent.bl.radius  <f> <px> <py> <radius_bits> <result>
///   ent.bl.collide <f> <target_x> <target_y> <hits>
///   ent.bl.near    <f> <px> <py> <dist> <result>
///   ent.bl.trap    <case> <spawn_x> <spawn_y> <spdx> <spdy> <player_y>
///                  <corridor> <player_x> <result>
void DumpEntityBullets(const HeadlessTextures& tex) {
  const ViewportMetrics m{kEntityWinW, kEntityWinH, 1.0f};
  BulletsManager bl(0xB011E75u);
  bl.SetMetrics(m);
  // One straight player bullet (collision bait), one relativistic alien bomb
  // (sinusoidal drift), one turning seeker bomb (spin then deflect at
  // trigger_y toward the player passed to Move).
  bl.Add(tex.Get(TextureId::Bullet), Coord(300, 600), Coord(0, -8));
  bl.Add(tex.Get(TextureId::Bomb), Coord(200, 100), Coord(3, 5),
         /*relativistic=*/true);
  bl.Add(tex.Get(TextureId::Bomb), Coord(900, 80), Coord(-2, 4),
         /*relativistic=*/false, /*is_turning=*/true, /*trigger_y=*/300,
         /*deflection_deg=*/17.5f);
  for (int i = 0; i < bl.Nb(); ++i) {
    const ProjectileSlot& b = bl.GetActive(i);
    std::printf("ent.bl.add %d %d %d %d %d %d %d %d %08x\n", i, b.pos.x,
                b.pos.y, b.speed.x, b.speed.y, b.relativistic ? 1 : 0,
                b.is_turning_bomb ? 1 : 0, b.turn_trigger_y,
                FloatBits(b.deflection_angle_deg));
  }

  // Synthetic interception target: hitbox derived from the headless player
  // dimensions. The straight bullet crosses y=320 exactly on frame 34, so the
  // collision probe below is a real intercept, not a miss.
  simulation::GameObject target{};
  target.renderable.pix = tex.Get(TextureId::Player);
  target.transform.position = Coord(300, 320);

  for (int f = 0; f < 110; ++f) {
    bl.Move(Coord(640, 660));
    int min_y = 0;
    int max_y = 0;
    bl.GetYBounds(min_y, max_y);
    std::printf("ent.bl.frame %d %d %d %d %d\n", f, bl.Nb(),
                bl.ActiveExhaustCount(), min_y, max_y);
    for (int i = 0; i < bl.Nb(); ++i) {
      const ProjectileSlot& b = bl.GetActive(i);
      std::printf(
          "ent.bl.slot %d %d %d %d %08x %08x %d %d %d %d %d %d %d %d %08x "
          "%08x %08x\n",
          f, i, b.pos.x, b.pos.y, FloatBits(b.fx), FloatBits(b.fy), b.speed.x,
          b.speed.y, b.relativistic ? 1 : 0, b.radioactive ? 1 : 0,
          b.wave_phase, b.is_turning_bomb ? 1 : 0, b.turn_completed ? 1 : 0,
          b.turn_timer, FloatBits(b.bomb_render_angle),
          FloatBits(b.deflection_angle_deg), FloatBits(b.speed_magnitude));
    }
    if (f % 10 == 0) {
      const int exhaust_n = std::min(bl.ActiveExhaustCount(), 8);
      for (int i = 0; i < exhaust_n; ++i) {
        const ExhaustParticle& e = bl.GetActiveExhaust(i);
        std::printf("ent.bl.exhaust %d %d %08x %08x %08x %08x %d\n", f, i,
                    FloatBits(e.pos.x), FloatBits(e.pos.y), FloatBits(e.vel.x),
                    FloatBits(e.vel.y), e.life);
      }
    }
    if (f == 34) {
      // Warp-trigger proximity gate: only player-side projectiles count
      // (speed.y < 0 and above the probe by 12px), and the two radii pin the
      // boundary behavior around the bullet crossing (300,320) this frame.
      const Coord probe{300, 360};
      const float radii[] = {60.0f, 20.0f};
      for (const float radius : radii) {
        std::printf("ent.bl.radius %d %d %d %08x %d\n", f, probe.x, probe.y,
                    FloatBits(radius),
                    bl.HasProjectileWithinRadius(probe, radius) ? 1 : 0);
      }
      std::printf("ent.bl.radius %d %d %d %08x %d\n", f, 900, 100,
                  FloatBits(50.0f),
                  bl.HasProjectileWithinRadius(Coord(900, 100), 50.0f) ? 1 : 0);
      std::printf("ent.bl.collide %d %d %d %d\n", f, target.Position().x,
                  target.Position().y, bl.DoCollisions(target, 2));
    }
    if (f == 80) {
      std::printf("ent.bl.near %d %d %d %d %d\n", f, 450, 510, 30,
                  bl.HasBombNear(Coord(450, 510), 30) ? 1 : 0);
      std::printf("ent.bl.near %d %d %d %d %d\n", f, 700, 200, 60,
                  bl.HasBombNear(Coord(700, 200), 60) ? 1 : 0);
      // Fair-play trap gate: pure integer verdicts (convergence + crossing
      // tests) against the live pool state of this exact frame.
      const Coord spawns[] = {Coord(400, 100), Coord(500, 100),
                              Coord(950, 100), Coord(60, 100)};
      const Coord speeds[] = {Coord(0, 5), Coord(-2, 5), Coord(0, 9),
                              Coord(2, 4)};
      const int players_x[] = {640, 400, 1200, 640};
      for (int i = 0; i < 4; ++i) {
        std::printf("ent.bl.trap %d %d %d %d %d %d %d %d %d\n", i, spawns[i].x,
                    spawns[i].y, speeds[i].x, speeds[i].y, 660,
                    GameRules::Fleet::kSafeEvasionCorridorPixels, players_x[i],
                    bl.WouldTrapPlayer(spawns[i], speeds[i], 660,
                                       GameRules::Fleet::kSafeEvasionCorridorPixels,
                                       players_x[i], kEntityWinW)
                        ? 1
                        : 0);
      }
    }
  }
}

/// Alien14 relativistic-warp replay. Row layouts:
///   ent.wp.seed  <trajectory_seed>
///   ent.wp.step  <step>
///   ent.wp.can   <step> <can_trigger>
///   ent.wp.occ   <step> <12 x occupancy_bits> <player_x> <player_y> <rows>
///   ent.wp.frame <step> <f> <stage> <x> <y> <angle_bits> <evasions> <warping>
///   ent.wp<N>.sink <f> ...   (cosmetic events of step N)
void DumpEntityWarp(const HeadlessTextures& tex) {
  RecordingSink sink;
  const ViewportMetrics m{kEntityWinW, kEntityWinH, 1.0f};
  const Coord base_cruise(300, 82);
  // Birth-roll seed search: first seed whose Alien14 is born warp-capable
  // (p = kAlien14WarpEvasionProbability on the trajectory RNG). The search is
  // deterministic on both sides of the port; the chosen seed is a corpus row.
  std::uint32_t chosen = 0;
  std::unique_ptr<Alien> ship;
  for (std::uint32_t s = 1; s <= 4096; ++s) {
    const Trajectory t(&EntityArrivalLoop(), false, base_cruise, &m, 3, 1, s,
                       1, GameRules::Fleet::FormationGrid::kMaxGridCols);
    auto candidate = std::make_unique<Alien>(tex.Get(TextureId::Alien14), t,
                                             6.0f, TextureId::Alien14);
    if (candidate->WarpEvasionsRemaining() > 0) {
      chosen = s;
      ship = std::move(candidate);
      break;
    }
  }
  if (!ship) {
    std::fprintf(stderr,
                 "dump_vectors: no warp-capable Alien14 seed found (1..4096)\n");
    std::exit(1);
  }
  std::printf("ent.wp.seed %u\n", chosen);
  ship->SetPresentationSink(&sink);
  ship->ForceCruise(ship->GetTrajectory().CruiseTarget());

  // Occupancy literal that exercises both the direct draw and the occupation
  // scan: row 7 partially full, row 9 striped, every other row free.
  // used_cols=16 keeps the two Chebyshev exclusion zones (around the ship and
  // around the player cell) from covering the whole domain, so the Priority-1
  // draw stays the live path.
  const std::array<std::uint16_t,
                   GameRules::Fleet::FormationGrid::kMaxGridRows>
      occupancy{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0x00FFu, 0u, 0x0F0Fu, 0u, 0u};

  for (int step = 0; step < 3; ++step) {
    std::printf("ent.wp.step %d\n", step);
    std::printf("ent.wp.can %d %d\n", step, ship->CanTriggerWarp() ? 1 : 0);
    std::printf("ent.wp.occ %d", step);
    for (const std::uint16_t bits : occupancy) {
      std::printf(" %u", static_cast<unsigned>(bits));
    }
    std::printf(" %d %d %d\n", 640, 660,
                GameRules::Fleet::FormationGrid::kMaxGridRows);
    // Step 2 runs with the evasion budget exhausted: the spec mandates a
    // silent no-op, and the frames that follow must show pure cruise drift.
    ship->TriggerSpacetimeWarp(
        occupancy, 640, 660,
        GameRules::Fleet::FormationGrid::kMaxGridRows);
    const int frames = (step == 2) ? 30 : 150;
    char prefix[16];
    std::snprintf(prefix, sizeof(prefix), "ent.wp%d", step);
    for (int f = 0; f < frames; ++f) {
      ship->Move();
      std::printf("ent.wp.frame %d %d %d %d %d %08x %d %d\n", step, f,
                  static_cast<int>(ship->Stage()), ship->Position().x,
                  ship->Position().y, FloatBits(ship->Angle()),
                  ship->WarpEvasionsRemaining(), ship->IsWarping() ? 1 : 0);
      sink.Drain(prefix, f);
    }
  }
}

/// Alien4 alpha-decay replay. Row layouts:
///   ent.dc.seed    <trajectory_seed>
///   ent.dc.trigger <phase> <scale_bits> <can_decay>
///   ent.dc.frame   <phase> <f> <stage> <x> <y> <angle_bits> <scale_bits>
///                  <can_decay>
void DumpEntityDecay(const HeadlessTextures& tex) {
  RecordingSink sink;
  const ViewportMetrics m{kEntityWinW, kEntityWinH, 1.0f};
  const Coord base_cruise(300, 82);
  // Birth-roll seed search: first seed whose Alien4 carries a live
  // electrosphere (two electron-phase draws precede the probability roll, so
  // the draw ORDER is part of the contract).
  std::uint32_t chosen = 0;
  std::unique_ptr<Alien> ship;
  for (std::uint32_t s = 1; s <= 4096; ++s) {
    const Trajectory t(&EntityArrivalLoop(), false, base_cruise, &m, 4, 2, s,
                       1, 8);
    auto candidate = std::make_unique<Alien>(tex.Get(TextureId::Alien4), t,
                                             6.0f, TextureId::Alien4);
    if (candidate->CanTriggerAlphaDecay()) {
      chosen = s;
      ship = std::move(candidate);
      break;
    }
  }
  if (!ship) {
    std::fprintf(stderr,
                 "dump_vectors: no electrosphere Alien4 seed found (1..4096)\n");
    std::exit(1);
  }
  std::printf("ent.dc.seed %u\n", chosen);
  ship->SetPresentationSink(&sink);
  ship->ForceCruise(ship->GetTrajectory().CruiseTarget());

  // Three phases: baseline cruise, the decay trigger (scale 0.5 and the x1.3
  // cruise speed must show up in the frame deltas) and a second trigger that
  // must be a silent no-op (health is now 1; only the second hit can kill).
  const int phase_frames[] = {30, 60, 30};
  for (int phase = 0; phase < 3; ++phase) {
    if (phase > 0) {
      ship->TriggerAlphaDecay();
      std::printf("ent.dc.trigger %d %08x %d\n", phase,
                  FloatBits(ship->GetGameObject().scale),
                  ship->CanTriggerAlphaDecay() ? 1 : 0);
    }
    for (int f = 0; f < phase_frames[phase]; ++f) {
      ship->Move();
      std::printf("ent.dc.frame %d %d %d %d %d %08x %08x %d\n", phase, f,
                  static_cast<int>(ship->Stage()), ship->Position().x,
                  ship->Position().y, FloatBits(ship->Angle()),
                  FloatBits(ship->GetGameObject().scale),
                  ship->CanTriggerAlphaDecay() ? 1 : 0);
    }
  }
}

/// Kamikaze guided dive through the full Alien state machine. Row layouts:
///   ent.kz.seed   <trajectory_seed>
///   ent.kz.init   <x> <y>
///   ent.kz.target <x> <y>
///   ent.kz.frame  <f> <stage> <x> <y> <angle_bits> <kamikaze>
void DumpEntityKamikaze(const HeadlessTextures& tex) {
  const ViewportMetrics m{kEntityWinW, kEntityWinH, 1.0f};
  const Coord base_cruise(300, 82);
  const Coord target(640, 640);
  // 420 frames cover the whole cycle: guided dive -> off-screen wrap ->
  // re-dock (joining) -> cruise. Same trajectory family as corpus case 3.
  const Trajectory t(&EntityArrivalLoop(), false, base_cruise, &m, 2, 4, 4242u,
                     3, 8);
  Alien ship(tex.Get(TextureId::Alien7), t, 6.0f, TextureId::Alien7);
  ship.SetKamikaze(true);
  ship.ForceCruise(t.CruiseTarget());
  std::printf("ent.kz.seed 4242\n");
  std::printf("ent.kz.init %d %d\n", ship.Position().x, ship.Position().y);
  std::printf("ent.kz.target %d %d\n", target.x, target.y);
  ship.BuildKamikazeDive(target);
  for (int f = 0; f < 420; ++f) {
    ship.Move();
    std::printf("ent.kz.frame %d %d %d %d %08x %d\n", f,
                static_cast<int>(ship.Stage()), ship.Position().x,
                ship.Position().y, FloatBits(ship.Angle()),
                ship.IsKamikaze() ? 1 : 0);
  }
}

/// Wave level of the fleet contract: level 3's profile (kCountsS1[2]) demands
/// exactly 4 convoys -- matching the synthetic table below -- and its
/// kamikaze quota (WaveToStage(3) = 1) guarantees one guided diver per run.
constexpr int kFleetLevel = 3;
/// Replay budget in frames. The seed search guarantees the scenario milestones
/// fit inside it; the replay stops early once the armada is finished and every
/// pool drained (recorded in ent.fleet.done).
constexpr int kFleetReplayFrames = 900;

/// Scenario-coverage flags collected during a replay; the seed search accepts
/// a seed only when every special-entity mechanic fired at least once, so the
/// corpus can never contain a fleet replay without warp/decay/bonus action.
struct FleetOutcome {
  bool warp_spawned{false};
  bool warp_used{false};
  bool decay_spawned{false};
  bool bonus_spawned{false};
  int kills{0};
};

/// Runs one seeded fleet replay in the EXACT production tick order
/// (CombatSession::TickFixedPhysics). With record=true it emits the
/// ent.fleet.* rows; the section is self-contained (convoy table, resampled
/// arrival paths and per-frame inputs are all rows) so the Rust replayer
/// needs no other corpus section. Returns whether the run satisfied the
/// coverage contract. Row layouts:
///   ent.fleet.setup  <level> <seed> <win_w> <win_h> <scale_bits>
///                    <frame_cap> <lethal_radius_bits>
///   ent.fleet.convoy <i> <wait> <texture_id> <size_pc> <mirror> <split>
///                    <simultaneous> <dock_align> <path_id>
///   ent.fleet.path_count <path_id> <n>
///   ent.fleet.path   <path_id> <j> <x_bits> <y_bits>
///   ent.fleet.frame  <f> <dir> <fire> <aliens> <bombs> <bullets> <bonuses>
///                    <explosions> <pl_x> <pl_y> <shield> <fire_lvl> <multi>
///                    <speed_lvl> <hits> <regular_hits> <kamikaze_hits>
///                    <score_total> <proximity>
///   ent.fleet.alien  <f> <i> <texture_id> <x> <y> <w> <h> <stage> <kamikaze>
///                    <warping> <evasions> <can_decay> <angle_bits> <scale_bits>
///   ent.fleet.bomb   <f> <i> <x> <y> <rel> <radioactive> <turning>
///                    <turn_timer> <wave_phase>
///   ent.fleet.bullet <f> <i> <x> <y> <spdx> <spdy>
///   ent.fleet.bonus  <f> <i> <type> <x> <y>
///   ent.fleet.sink   <f> ...     (cosmetic events of that frame)
///   ent.fleet.done   <frames> <finished> <kills>
/// The Fatia-8 companion section pins the SESSION-level score context (the
/// TickFixedPhysics block between the bullet and bomb collisions: credit ->
/// milestone drain -> ExtraShield). The 900-frame window earns less than one
/// milestone, so the replay rows prove the wiring stays inert there, and a
/// scripted post-replay award cascade (1000/2000/5000 -> exactly 1/2/5
/// milestones) exercises the drain and the shield clamp. Row layouts:
///   ent.fs.setup <seed> <initial_shield> <extra_shield_step> <max_shield>
///   ent.fs.frame <f> <score_total> <progress_bits> <shield> <extra_shields>
///                (one row per credited frame, end-of-frame values)
///   ent.fs.award <i> <pts> <milestones> <shield> <progress_bits>
///                <score_total>
///   ent.fs.done  <frames> <credited_frames> <final_total> <final_shield>
///                <awarded_milestones>
bool RunFleetReplay(std::uint32_t seed, bool record,
                    const HeadlessTextures& tex, FleetOutcome& out) {
  const ViewportMetrics m{kEntityWinW, kEntityWinH, 1.0f};
  // Synthetic convoy table (see kFleetLevel): Alien14 first so warp-capable
  // ships exist, two Alien4 convoys so the electrosphere roll almost surely
  // fires, split/mirror variety on the spline arrivals, and short waits so
  // the combat window dominates the replay.
  const ConvoyData convoys[] = {
      {0, TextureId::Alien14, 25, &EntityArrivalLoop(), false, false},
      {10, TextureId::Alien4, 30, &EntityArrivalArc(), true, true},
      {10, TextureId::Alien1, 20, &EntityArrivalSpire(), false, false},
      {15, TextureId::Alien4, 25, &EntityArrivalLoop(), true, false},
  };
  constexpr int kPathIds[] = {0, 1, 2, 0};
  const FlightPath* const paths[] = {&EntityArrivalLoop(), &EntityArrivalArc(),
                                     &EntityArrivalSpire()};

  const Config cfg = Config::CreateHeadless();
  RecordingSink sink;
  BulletsManager bullets(0x8EED11u);
  BulletsManager bombs(0x8EED22u);
  BonusManager bonuses(tex);
  ExplosionsManager explosions(cfg);
  explosions.SeedRandom(0xE1E2E3u);
  bullets.SetMetrics(m);
  bombs.SetMetrics(m);
  bonuses.SetMetrics(m);
  explosions.SetMetrics(m);
  // Production wiring minus the shell: score stays null (every consumer
  // guards it) and sink/textures are the recording doubles.
  AliensManager aliens(&bombs, &bullets, &bonuses, &explosions,
                       /*score=*/nullptr, sink, tex, m, kFleetLevel, convoys,
                       GameRules::Progression::kWavesPerStage, seed);
  Player player(&bullets, &bombs, &bonuses, &explosions, &cfg, sink, tex, m);
  aliens.SetPlayer(&player);
  const float lethal =
      static_cast<float>(GameRules::Fleet::FormationGrid::Width()) *
      GameRules::Fleet::kKamikazeBlastRadiusMultiplier;

  // Fatia 8: the session-level score context. Headless Score (Fatia 7) plus
  // the exact production block of TickFixedPhysics -- credit, drain the
  // extra-shield milestones, apply each to the player. The SFX the C++ shell
  // plays per milestone is presentation; the golden rows pin the counts.
  Score score;
  const auto score_context = [&](std::uint64_t earned) -> std::uint64_t {
    if (earned > 0) {
      score.Add(earned);
    }
    const std::uint64_t milestones = score.CheckExtraShieldMilestones();
    for (std::uint64_t mm = 0; mm < milestones; ++mm) {
      player.ExtraShield();
    }
    return milestones;
  };
  std::uint64_t replay_milestones = 0;
  int fs_credit_frames = 0;
  std::ostringstream fs;
  if (record) {
    fs << "ent.fs.setup " << seed << ' ' << player.Shield() << ' '
       << GameRules::Combat::kExtraShieldScoreStep << ' '
       << GameRules::Player::kPlayerMaxShield << '\n';
  }

  if (record) {
    std::printf("ent.fleet.setup %d %u %d %d %08x %d %08x\n", kFleetLevel,
                seed, kEntityWinW, kEntityWinH, FloatBits(m.scale),
                kFleetReplayFrames, FloatBits(lethal));
    for (int c = 0; c < 4; ++c) {
      const ConvoyData& cv = convoys[c];
      std::printf("ent.fleet.convoy %d %d %d %d %d %d %d %d %d\n", c, cv.wait,
                  static_cast<int>(cv.texture_id), cv.convoy_size_pc,
                  cv.x_mirror ? 1 : 0, cv.split ? 1 : 0,
                  cv.simultaneous ? 1 : 0, static_cast<int>(cv.dock_align),
                  kPathIds[c]);
    }
    for (int p = 0; p < 3; ++p) {
      std::printf("ent.fleet.path_count %d %zu\n", p, paths[p]->Size());
      for (std::size_t j = 0; j < paths[p]->Size(); ++j) {
        std::printf("ent.fleet.path %d %zu %08x %08x\n", p, j,
                    FloatBits((*paths[p])[j].x), FloatBits((*paths[p])[j].y));
      }
    }
  }

  InputScript script(0x1234ABCDu);
  std::uint64_t score_total = 0;
  int f = 0;
  for (; f < kFleetReplayFrames; ++f) {
    script.Step();
    const int dir = script.Dir();

    // Exact production tick order (CombatSession::TickFixedPhysics):
    // collisions first, then bonuses/player/projectiles, fleet last. The
    // player fires every frame; the fire interval gates the real cadence.
    const BulletCollisionSummary summary = aliens.DoBulletsCollisions();
    const std::uint64_t frame_extra = score_context(summary.score_earned);
    replay_milestones += frame_extra;
    player.DoBombsCollisions();
    const bool prox = aliens.CheckKamikazeProximity(player.Position(), lethal);
    player.DoBonusCollisions(&aliens);
    bonuses.Move();
    player.Move(dir);
    bullets.Move();
    bombs.Move(player.Position());
    player.Fire();
    explosions.Move();
    aliens.Move();
    aliens.Fire(player.Position());

    score_total += summary.score_earned;
    out.kills += summary.total_hits;
    if (bonuses.Nb() > 0 ||
        player.SpeedLevel() + player.FireLevel() + player.MultiFire() > 1) {
      out.bonus_spawned = true;
    }
    for (std::size_t i = 0; i < aliens.AlienCount(); ++i) {
      const Alien& a = aliens.GetAlien(i);
      if (a.GetTextureId() == TextureId::Alien14) {
        if (a.WarpEvasionsRemaining() > 0) out.warp_spawned = true;
        if (a.WarpEvasionsRemaining() <
            GameRules::SpecialEntities::kAlien14NumWarpEvasions) {
          out.warp_used = true;
        }
      } else if (a.GetTextureId() == TextureId::Alien4 &&
                 (a.CanTriggerAlphaDecay() ||
                  a.GetGameObject().scale < 1.0f)) {
        out.decay_spawned = true;
      }
    }

    if (record) {
      std::printf(
          "ent.fleet.frame %d %d %d %zu %d %d %d %zu %d %d %d %d %d %d %d %d "
          "%d %llu %d\n",
          f, dir, 1, aliens.AlienCount(), bombs.Nb(), bullets.Nb(),
          bonuses.Nb(), explosions.ActiveCount(), player.Position().x,
          player.Position().y, player.Shield(), player.FireLevel(),
          player.MultiFire(), player.SpeedLevel(), summary.total_hits,
          summary.regular_hits, summary.kamikaze_hits,
          static_cast<unsigned long long>(score_total), prox ? 1 : 0);
      if (summary.score_earned > 0) {
        ++fs_credit_frames;
        char pbits[17];
        std::snprintf(pbits, sizeof(pbits), "%016llx",
                      static_cast<unsigned long long>(
                          DoubleBits(score.ShieldProgress())));
        fs << "ent.fs.frame " << f << ' ' << score.Value() << ' ' << pbits
           << ' ' << player.Shield() << ' ' << frame_extra << '\n';
      }
      for (std::size_t i = 0; i < aliens.AlienCount(); ++i) {
        const Alien& a = aliens.GetAlien(i);
        std::printf(
            "ent.fleet.alien %d %zu %d %d %d %d %d %d %d %d %d %d %08x %08x\n",
            f, i, static_cast<int>(a.GetTextureId()), a.Position().x,
            a.Position().y, a.Width(), a.Height(), static_cast<int>(a.Stage()),
            a.IsKamikaze() ? 1 : 0, a.IsWarping() ? 1 : 0,
            a.WarpEvasionsRemaining(), a.CanTriggerAlphaDecay() ? 1 : 0,
            FloatBits(a.Angle()), FloatBits(a.GetGameObject().scale));
      }
      for (int i = 0; i < bombs.Nb(); ++i) {
        const ProjectileSlot& b = bombs.GetActive(i);
        std::printf("ent.fleet.bomb %d %d %d %d %d %d %d %d %d\n", f, i,
                    b.pos.x, b.pos.y, b.relativistic ? 1 : 0,
                    b.radioactive ? 1 : 0, b.is_turning_bomb ? 1 : 0,
                    b.turn_timer, b.wave_phase);
      }
      for (int i = 0; i < bullets.Nb(); ++i) {
        const ProjectileSlot& b = bullets.GetActive(i);
        std::printf("ent.fleet.bullet %d %d %d %d %d %d\n", f, i, b.pos.x,
                    b.pos.y, b.speed.x, b.speed.y);
      }
      for (int i = 0; i < bonuses.Nb(); ++i) {
        const BonusSlot& s = bonuses.GetActive(i);
        std::printf("ent.fleet.bonus %d %d %d %d %d\n", f, i,
                    static_cast<int>(s.type), s.pos.x, s.pos.y);
      }
      sink.Drain("ent.fleet", f);
    }

    // Early exit: armada finished and every pool drained (only after the
    // search had a fair combat window to satisfy the coverage contract).
    if (f >= 500 && aliens.Finished() && bombs.Nb() == 0 && bullets.Nb() == 0 &&
        bonuses.Nb() == 0 && explosions.ActiveCount() == 0) {
      ++f;
      break;
    }
  }
  if (record) {
    std::printf("ent.fleet.done %d %d %d\n", f, aliens.Finished() ? 1 : 0,
                out.kills);

    // ent.fs.* epilogue: the replay window cannot reach a milestone (130
    // points < the 1000-point step), so the award cascade below is what
    // exercises the milestone drain and the ExtraShield clamp. Each amount
    // drains deterministically (1000 -> 1, 2000 -> 2, 5000 -> 5) regardless
    // of the replay residue, which only survives as the pinned progress bits.
    static constexpr std::uint64_t kFsAwardPts[] = {1000, 2000, 5000};
    std::uint64_t fs_awarded = 0;
    for (int a = 0; a < 3; ++a) {
      const std::uint64_t ms = score_context(kFsAwardPts[a]);
      fs_awarded += ms;
      char pbits[17];
      std::snprintf(pbits, sizeof(pbits), "%016llx",
                    static_cast<unsigned long long>(
                        DoubleBits(score.ShieldProgress())));
      fs << "ent.fs.award " << a << ' ' << kFsAwardPts[a] << ' ' << ms << ' '
         << player.Shield() << ' ' << pbits << ' ' << score.Value() << '\n';
    }

    // Coverage contract (hard gate, same discipline as the seed search): the
    // replay itself must stay milestone-free (proof the committed ent.fleet.*
    // rows are untouched by the Score wiring), the cascade must drain exactly
    // 1+2+5, and the shield must end clamped at the cap.
    if (replay_milestones != 0 || fs_credit_frames < 5 || fs_awarded != 8 ||
        player.Shield() != GameRules::Player::kPlayerMaxShield) {
      std::fprintf(stderr,
                   "dump_vectors: fleet score-context contract failed "
                   "(replay_ms=%llu credited=%d awarded=%llu shield=%d)\n",
                   static_cast<unsigned long long>(replay_milestones),
                   fs_credit_frames,
                   static_cast<unsigned long long>(fs_awarded),
                   player.Shield());
      std::exit(1);
    }
    fs << "ent.fs.done " << f << ' ' << fs_credit_frames << ' '
       << score.Value() << ' ' << player.Shield() << ' ' << fs_awarded << '\n';
    std::printf("%s", fs.str().c_str());
  }
  return out.warp_spawned && out.warp_used && out.decay_spawned &&
         out.bonus_spawned;
}

void DumpEntityFleet(const HeadlessTextures& tex) {
  // Deterministic seed search: the first seed that exercises warp evasion
  // (born AND consumed), alpha decay and the bonus economy inside the replay
  // budget. A miss would silently hollow out the contract, so failure here is
  // a hard error instead of an empty section.
  std::uint32_t chosen = 0;
  for (std::uint32_t s = 1; s <= 2048; ++s) {
    FleetOutcome probe;
    if (RunFleetReplay(s, false, tex, probe)) {
      chosen = s;
      break;
    }
  }
  if (chosen == 0) {
    std::fprintf(stderr,
                 "dump_vectors: no fleet seed satisfied the coverage "
                 "contract within 2048 candidates\n");
    std::exit(1);
  }
  FleetOutcome recorded;
  RunFleetReplay(chosen, true, tex, recorded);
}

/// Player-side economy replay (no armada): movement, fire cadence, the five
/// supply pods and the shield economy under a bomb volley. Row layouts:
///   ent.pl.setup  <win_w> <win_h> <shield>
///   ent.pl.frame  <f> <phase> <dir> <fire> <x> <y> <shield> <fire_lvl>
///                 <multi> <speed_lvl> <bullets> <bombs> <bonuses>
///                 <explosions>
///   ent.pl.bullet <f> <i> <x> <y> <spdx> <spdy>
///   ent.pl.bomb   <f> <i> <x> <y> <wave_phase>
///   ent.pl.drop   <f> <i> <type> <y>
///   ent.pl.bonus  <f> <i> <type> <x> <y>
///   ent.pl.sink   <f> ...   (cosmetic events of that frame)
void DumpEntityPlayer(const HeadlessTextures& tex) {
  const ViewportMetrics m{kEntityWinW, kEntityWinH, 1.0f};
  const Config cfg = Config::CreateHeadless();
  RecordingSink sink;
  BulletsManager bullets(0xD1CE71u);
  BulletsManager bombs(0xD1CE72u);
  BonusManager bonuses(tex);
  ExplosionsManager explosions(cfg);
  explosions.SeedRandom(0xD1CE73u);
  bullets.SetMetrics(m);
  bombs.SetMetrics(m);
  bonuses.SetMetrics(m);
  explosions.SetMetrics(m);
  Player player(&bullets, &bombs, &bonuses, &explosions, &cfg, sink, tex, m);
  std::printf("ent.pl.setup %d %d %d\n", kEntityWinW, kEntityWinH,
              player.Shield());

  // Production tick order restricted to the player-owned systems (there is no
  // armada in this section): collisions -> bonuses -> movement -> projectiles
  // -> fire -> explosions.
  const auto tick = [&](int move_dir, bool want_fire) {
    player.DoBombsCollisions();
    player.DoBonusCollisions(nullptr);
    bonuses.Move();
    player.Move(move_dir);
    bullets.Move();
    bombs.Move(player.Position());
    if (want_fire) player.Fire();
    explosions.Move();
  };
  int f = 0;
  const auto record_frame = [&](int phase, int move_dir, bool want_fire) {
    std::printf("ent.pl.frame %d %d %d %d %d %d %d %d %d %d %d %d %d %zu\n", f,
                phase, move_dir, want_fire ? 1 : 0, player.Position().x,
                player.Position().y, player.Shield(), player.FireLevel(),
                player.MultiFire(), player.SpeedLevel(), bullets.Nb(),
                bombs.Nb(), bonuses.Nb(), explosions.ActiveCount());
    for (int i = 0; i < bullets.Nb(); ++i) {
      const ProjectileSlot& b = bullets.GetActive(i);
      std::printf("ent.pl.bullet %d %d %d %d %d %d\n", f, i, b.pos.x, b.pos.y,
                  b.speed.x, b.speed.y);
    }
    for (int i = 0; i < bombs.Nb(); ++i) {
      const ProjectileSlot& b = bombs.GetActive(i);
      std::printf("ent.pl.bomb %d %d %d %d %d\n", f, i, b.pos.x, b.pos.y,
                  b.wave_phase);
    }
    for (int i = 0; i < bonuses.Nb(); ++i) {
      const BonusSlot& s = bonuses.GetActive(i);
      std::printf("ent.pl.bonus %d %d %d %d %d\n", f, i,
                  static_cast<int>(s.type), s.pos.x, s.pos.y);
    }
    sink.Drain("ent.pl", f);
  };

  // Phase A -- scripted lateral movement + continuous fire (the base 40-frame
  // interval gates the real cadence).
  InputScript script(0x77777777u);
  for (; f < 120; ++f) {
    script.Step();
    const int dir = script.Dir();
    tick(dir, true);
    record_frame(0, dir, true);
  }

  // Phase B -- full bonus economy: all five supply pods queued above the
  // frozen ship, collected in falling order (speed, fire, multi, shield,
  // nuke). Upgrade levels and the cosmetic events of every collection land in
  // the frame/sink rows. With no armada the nuke pod only emits its banner.
  const int drop_x = player.Position().x;
  const Bonus::bonus_t drops[] = {Bonus::extra_speed, Bonus::extra_fire,
                                  Bonus::extra_multi, Bonus::extra_shield,
                                  Bonus::extra_nuke};
  for (int i = 0; i < 5; ++i) {
    const int drop_y = 560 - i * 24;
    bonuses.Add(Coord(drop_x, drop_y), drops[i]);
    std::printf("ent.pl.drop %d %d %d %d\n", f, i, static_cast<int>(drops[i]),
                drop_y);
  }
  for (; f < 400 && bonuses.Nb() > 0; ++f) {
    tick(0, true);
    record_frame(1, 0, true);
  }

  // Phase C -- bomb volley: shield economy plus the damage events (flash,
  // trauma, sfx and debris explosions). Three bombs against the 4-point
  // shield (3 initial + the phase-B restore) keep shield >= 0: the death
  // cinematic is shell territory, not simulation.
  for (int k = 0; k < 3; ++k) {
    bombs.Add(tex.Get(TextureId::Bomb), Coord(drop_x, -40 - k * 120),
              Coord(0, 6));
  }
  const int phase_c_end = f + 240;
  for (; f < phase_c_end; ++f) {
    tick(0, false);
    record_frame(2, 0, false);
  }
}

/// Explosion pool replay. Row layouts:
///   ent.ex.seed  <seed>
///   ent.ex.add   <i> <x> <y> <spdx> <spdy> <r> <g> <b> <duration>
///   ent.ex.frame  <f> <active>        (phase 1: three bursts)
///   ent.ex.p      <f> <i> <posx_bits> <posy_bits> <velx_bits> <vely_bits>
///                 <r> <g> <b> <a> <life> <max_life>
///   ent.ex.clear <active>
///   ent.ex.nova  <active>
///   ent.ex2.frame / ent.ex2.p         (phase 2: screen-wide nova)
void DumpEntityExplosions() {
  const ViewportMetrics m{kEntityWinW, kEntityWinH, 1.0f};
  const Config cfg = Config::CreateHeadless();
  ExplosionsManager ex(cfg);
  ex.SeedRandom(0x9E3779B9u);
  ex.SetMetrics(m);
  std::printf("ent.ex.seed %u\n", 0x9E3779B9u);

  struct Burst {
    Coord pos;
    Coord vel;
    int r;
    int g;
    int b;
    int duration;
  };
  const Burst bursts[] = {
      {Coord(640, 360), Coord(0, -2), 255, 255, 200, 24},
      {Coord(300, 200), Coord(2, 1), 45, 255, 95, 26},
      {Coord(900, 500), Coord(-1, -3), 255, 230, 90, 36},
  };
  for (int i = 0; i < 3; ++i) {
    const Burst& b = bursts[i];
    ex.Add(b.pos, b.vel, static_cast<std::uint8_t>(b.r),
           static_cast<std::uint8_t>(b.g), static_cast<std::uint8_t>(b.b),
           b.duration);
    std::printf("ent.ex.add %d %d %d %d %d %d %d %d %d\n", i, b.pos.x,
                b.pos.y, b.vel.x, b.vel.y, b.r, b.g, b.b, b.duration);
  }
  // Sampled particle windows: the first N active particles of the dense list,
  // every K frames -- enough to pin the physics (drag, gravity, fade) without
  // flooding the corpus with the full 4096-slot pool.
  const auto sample = [&](const char* section, int base_frame,
                          std::size_t max_samples) {
    std::printf("%s.frame %d %zu\n", section, base_frame, ex.ActiveCount());
    const std::size_t samples = std::min(ex.ActiveCount(), max_samples);
    for (std::size_t i = 0; i < samples; ++i) {
      const Particle& p = ex.GetActiveParticle(i);
      std::printf("%s.p %d %zu %08x %08x %08x %08x %d %d %d %d %d %d\n",
                  section, base_frame, i, FloatBits(p.pos.x),
                  FloatBits(p.pos.y), FloatBits(p.vel.x), FloatBits(p.vel.y),
                  p.r, p.g, p.b, p.a, p.life, p.max_life);
    }
  };
  for (int f = 0; f < 70; ++f) {
    ex.Move();
    if (f % 5 == 0) sample("ent.ex", f, 6);
  }
  ex.Clear();
  std::printf("ent.ex.clear %zu\n", ex.ActiveCount());
  // Screen-wide nova: fills the pool to capacity; the grid bursts that follow
  // are no-ops once the free list empties, which is itself part of the
  // contract (recorded by the active count).
  ex.TriggerScreenWideNova(Coord(640, 640), kEntityWinW, kEntityWinH);
  std::printf("ent.ex.nova %zu\n", ex.ActiveCount());
  for (int f = 0; f < 40; ++f) {
    ex.Move();
    if (f % 10 == 0) sample("ent.ex2", f, 4);
  }
}

/// FNV-1a (64) over the full dynamic state of every active star, in pool
/// order: one row-sized pin for the whole field per frame. Byte-serializes
/// the float bits little-endian so the Rust test recomputes it identically.
[[nodiscard]] std::uint64_t StarPoolHash(const StarsFields& stars) noexcept {
  std::uint64_t h = 14695981039346656037ull;
  const auto feed_byte = [&h](std::uint8_t b) noexcept {
    h = (h ^ b) * 1099511628211ull;
  };
  const auto feed_u32 = [&feed_byte](std::uint32_t v) noexcept {
    feed_byte(static_cast<std::uint8_t>(v));
    feed_byte(static_cast<std::uint8_t>(v >> 8));
    feed_byte(static_cast<std::uint8_t>(v >> 16));
    feed_byte(static_cast<std::uint8_t>(v >> 24));
  };
  for (size_t i = 0; i < stars.ActiveStars(); ++i) {
    const StarsFields::StarSample s = stars.SampleStar(i);
    feed_u32(FloatBits(s.x));
    feed_u32(FloatBits(s.y));
    feed_u32(FloatBits(s.base_speed));
    feed_byte(s.type);
    feed_byte(s.spectral);
    feed_byte(s.r);
    feed_byte(s.g);
    feed_byte(s.b);
    feed_byte(s.alpha);
    feed_u32(s.twinkle_phase);
    feed_u32(s.twinkle_step);
  }
  return h;
}

/// Starfield replay (RUST.md Fatia 6). The twinkle LUT and the hyperspace
/// warp easing contain transcendentals (sinf/cosf): the corpus FREEZES them
/// as recorded data so the Rust port replays bit-exact on any platform --
/// the frozen-table policy of textures::HEADLESS_DIMS applied to curves.
/// Scenario: 640 Scroll() frames over a 1280x720 headless viewport with the
/// production-length hyperspace warp at f60 (the phase-transition frame
/// count), a 1600x900 viewport growth at f480 (density expansion) and a
/// details drop 2->1 at f560 (density shrink). Row layouts:
///   ent.stars.setup    <key> <values...>   (SSOT echo: constants + scenario)
///   ent.stars.alpha    <i> <v>             (twinkle LUT, uint8)
///   ent.stars.pulse    <i> <bits>          (Pulse01 of the LUT sample, f32)
///   ent.stars.warp     <t> <bits>          (warp_factor by remaining timer)
///   ent.stars.frame    <f> <warp_timer> <warp_factor bits> <active> <win_w>
///                      <win_h> <details> <respawned> <pool_hash>
///   ent.stars.star     <f> <i> <x> <y> <speed> <type> <spectral> <r> <g>
///                      <b> <phase> <step> <alpha>   (sampled slots, every f)
///   ent.stars.snapshot <f> <i> <... same 11 data columns ...>  (full pool)
///   ent.stars.done     <frames> <respawns_total> <final_active>
void DumpStarfield() {
  namespace SF = GameRules::Starfield;
  constexpr int kWinW = 1280;
  constexpr int kWinH = 720;
  constexpr int kFrames = 640;
  constexpr int kWarpAt = 60;
  constexpr int kResizeAt = 480;
  constexpr int kNewW = 1600;
  constexpr int kNewH = 900;
  constexpr int kDensityAt = 560;
  constexpr size_t kSampled = 8;
  // The exact production call: the phase transition triggers the warp with
  // FastRound(kStageFanfareDurationSeconds * kSimulationFrequencyHz) frames
  // (combat_session.cc), so the replay mirrors that derivation.
  const int warp_frames = FastRound(
      static_cast<double>(GameRules::Progression::kStageFanfareDurationSeconds) *
      static_cast<double>(GameRules::Simulation::kSimulationFrequencyHz));

  std::printf("ent.stars.setup seed %08x\n", 0xC0531CC1u);
  std::printf("ent.stars.setup win %d %d\n", kWinW, kWinH);
  std::printf("ent.stars.setup frames %d\n", kFrames);
  std::printf("ent.stars.setup warp_frames %d\n", warp_frames);
  std::printf("ent.stars.setup warp_at %d\n", kWarpAt);
  std::printf("ent.stars.setup resize_at %d %d %d\n", kResizeAt, kNewW, kNewH);
  std::printf("ent.stars.setup density_at %d\n", kDensityAt);
  std::printf("ent.stars.setup warp_mult %08x\n",
              FloatBits(GameRules::Progression::kHyperspaceWarpSpeedMultiplier));
  std::printf("ent.stars.setup base_res %.0f %.0f\n",
              SF::kBaseResolutionWidthPixels, SF::kBaseResolutionHeightPixels);
  std::printf("ent.stars.setup base_count %.0f\n", SF::kBaseStarCount);
  std::printf("ent.stars.setup min_max %zu %zu\n", SF::kMinStarsCount,
              SF::kMaxStarsCount);
  std::printf("ent.stars.setup threshold %08x\n",
              FloatBits(SF::kForegroundLayerThreshold));
  std::printf("ent.stars.setup tau %08x\n", FloatBits(SF::kTau));
  std::printf("ent.stars.setup twinkle_floor_ceil %08x %08x\n",
              FloatBits(SF::kTwinkleFloor), FloatBits(SF::kTwinkleCeil));
  std::printf("ent.stars.setup harmonics %08x %08x\n",
              FloatBits(SF::kTwinkleHarmonic2), FloatBits(SF::kTwinkleHarmonic3));
  std::printf("ent.stars.setup mid_speed %08x %08x\n",
              FloatBits(SF::kMidFieldFallSpeedMinPx),
              FloatBits(SF::kMidFieldFallSpeedMaxPx));
  std::printf("ent.stars.setup fg_speed %08x %08x\n",
              FloatBits(SF::kForegroundFallSpeedMinPx),
              FloatBits(SF::kForegroundFallSpeedMaxPx));
  std::printf("ent.stars.setup twinkle_speed %08x %08x\n",
              FloatBits(SF::kTwinkleSpeedMin), FloatBits(SF::kTwinkleSpeedMax));
  std::printf("ent.stars.setup phase_scale %u\n", SF::kTwinklePhaseScale);
  std::printf("ent.stars.setup density_factors %016llx %016llx %016llx %016llx %016llx\n",
              static_cast<unsigned long long>(DoubleBits(SF::GetDensityFactor(0))),
              static_cast<unsigned long long>(DoubleBits(SF::GetDensityFactor(1))),
              static_cast<unsigned long long>(DoubleBits(SF::GetDensityFactor(2))),
              static_cast<unsigned long long>(DoubleBits(SF::GetDensityFactor(3))),
              static_cast<unsigned long long>(DoubleBits(SF::GetDensityFactor(4))));

  Config cfg = Config::CreateHeadless();
  StarsFields stars(kWinW, kWinH);
  stars.SetConfig(&cfg);

  for (int i = 0; i < 256; ++i) {
    std::printf("ent.stars.alpha %d %u\n", i,
                static_cast<unsigned>(
                    StarsFields::TwinkleAlpha(static_cast<std::uint8_t>(i))));
  }
  for (int i = 0; i < 256; ++i) {
    std::printf("ent.stars.pulse %d %08x\n", i,
                FloatBits(StarsFields::TwinklePulse(static_cast<std::uint8_t>(i))));
  }

  // Warp easing probe: the curve is a pure function of the remaining timer
  // (no RNG, no viewport), so a throwaway instance records the frozen table.
  {
    StarsFields probe(kWinW, kWinH);
    probe.SetConfig(&cfg);
    probe.TriggerHyperspaceWarp(warp_frames);
    for (int t = 0; t < warp_frames; ++t) {
      probe.Scroll();
      std::printf("ent.stars.warp %d %08x\n", probe.WarpTimer(),
                  FloatBits(probe.WarpFactor()));
    }
  }

  std::uint64_t respawns_total = 0;
  bool warp_seen = false;
  bool expanded = false;
  bool shrunk = false;
  for (int f = 0; f < kFrames; ++f) {
    if (f == kWarpAt) stars.TriggerHyperspaceWarp(warp_frames);
    if (f == kResizeAt) stars.SetViewport(kNewW, kNewH);
    if (f == kDensityAt) cfg.AddDetailsLevel(-1);
    stars.Scroll();
    if (stars.WarpTimer() > 0 || stars.WarpFactor() > 1.0f) warp_seen = true;

    // Respawn observable: the headless fall respawn parks y at exactly -5.
    int respawned = 0;
    for (size_t i = 0; i < stars.ActiveStars(); ++i) {
      if (stars.SampleStar(i).y == -5.0f) ++respawned;
    }
    respawns_total += static_cast<std::uint64_t>(respawned);
    if (stars.ActiveStars() == 225) expanded = true;
    if (f >= kDensityAt && stars.ActiveStars() == 90) shrunk = true;

    std::printf("ent.stars.frame %d %d %08x %zu %d %d %d %d %016llx\n", f,
                stars.WarpTimer(), FloatBits(stars.WarpFactor()),
                stars.ActiveStars(), stars.ViewportWidth(),
                stars.ViewportHeight(), cfg.DetailsLevel(), respawned,
                static_cast<unsigned long long>(StarPoolHash(stars)));

    const bool snapshot = (f == 0 || f == 420 || f == kResizeAt ||
                           f == kDensityAt || f == kFrames - 1);
    const size_t rows = snapshot ? stars.ActiveStars() : kSampled;
    for (size_t i = 0; i < rows; ++i) {
      const StarsFields::StarSample s = stars.SampleStar(i);
      std::printf("%s %d %zu %08x %08x %08x %u %u %u %u %u %u %u %u\n",
                  snapshot ? "ent.stars.snapshot" : "ent.stars.star", f, i,
                  FloatBits(s.x), FloatBits(s.y), FloatBits(s.base_speed),
                  static_cast<unsigned>(s.type), static_cast<unsigned>(s.spectral),
                  static_cast<unsigned>(s.r), static_cast<unsigned>(s.g),
                  static_cast<unsigned>(s.b),
                  static_cast<unsigned>(s.twinkle_phase),
                  static_cast<unsigned>(s.twinkle_step),
                  static_cast<unsigned>(s.alpha));
    }
  }

  // Coverage contract (hard gate, same discipline as the fleet seed search):
  // a scenario that silently stopped warping/respawning/resizing would hollow
  // out the section, so failure here aborts the dump instead of recording it.
  if (!warp_seen || respawns_total < 10 || !expanded || !shrunk ||
      stars.ActiveStars() != 90) {
    std::fprintf(stderr,
                 "dump_vectors: starfield coverage contract failed "
                 "(warp=%d respawns=%llu expanded=%d shrunk=%d active=%zu)\n",
                 warp_seen ? 1 : 0,
                 static_cast<unsigned long long>(respawns_total),
                 expanded ? 1 : 0, shrunk ? 1 : 0, stars.ActiveStars());
    std::exit(1);
  }
  std::printf("ent.stars.done %d %llu %zu\n", kFrames,
              static_cast<unsigned long long>(respawns_total),
              stars.ActiveStars());
}

// ---- score.* : Score pure-logic replay (Fatia 7) ----
// A scripted op table: every op runs against a headless Score and echoes the
// FULL state, so the Rust twin replays the operations from the corpus rows
// themselves (the script never exists twice). The ops exercise the saturating
// Add, the fractional milestone ledger (double bits pinned), multi-milestone
// drains, shield clamps (including the negative destruction sentinel), the
// wave-to-stage cycle rollovers that reset the bonus-economy mask, the cheat
// flag and ReInit. The saturating Add leaves shield_progress_ astronomically
// large and the milestone drain is a subtractive loop, so the script must
// never run `milestone` between that Add and the following ReInit (production
// never Adds anywhere near those magnitudes either).
void DumpScore() {
  std::printf("score.consts %d %d %llu %d %d\n",
              GameRules::Player::kInitialShields,
              GameRules::Player::kPlayerMaxShield,
              static_cast<unsigned long long>(GameRules::Combat::kExtraShieldScoreStep),
              GameRules::Progression::kWavesPerStage,
              GameRules::Combat::kMaxBonusesPerStage);
  std::printf("score.bonus_enum %d %d %d %d %d\n",
              Bonus::extra_speed, Bonus::extra_fire, Bonus::extra_shield,
              Bonus::extra_multi, Bonus::extra_nuke);

  Score s;
  int op_index = 0;
  bool saw_saturation = false;
  bool saw_multi_milestone = false;
  bool saw_rollover_reallow = false;
  int max_bonus_count = 0;

  const auto row = [&](const char* op, unsigned long long arg,
                       unsigned long long result) {
    std::printf("score.op %d %s %llu %llu %016llx %d %d %d %d %u %d %llu\n",
                op_index++, op, arg,
                static_cast<unsigned long long>(s.Value()),
                static_cast<unsigned long long>(DoubleBits(s.ShieldProgress())),
                s.Shield(), s.Level(), s.Cycle(), s.Cheated() ? 1 : 0,
                s.StageBonusMask(), s.StageBonusCycle(), result);
  };
  const auto add = [&](unsigned long long pts) {
    s.Add(static_cast<uint64_t>(pts));
    if (s.Value() == UINT64_MAX) saw_saturation = true;
    row("add", pts, 0);
  };
  const auto milestone = [&]() {
    const uint64_t n = s.CheckExtraShieldMilestones();
    if (n > 1) saw_multi_milestone = true;
    row("milestone", 0, static_cast<unsigned long long>(n));
  };
  const auto shield = [&](int req) {
    s.SetShield(req);
    row("shield", static_cast<unsigned long long>(static_cast<long long>(req)), 0);
  };
  const auto inc = [&]() { s.IncLevel(); row("inc", 0, 0); };
  const auto cheat = [&](bool c) { s.SetCheated(c); row("cheat", c ? 1ULL : 0ULL, 0); };
  const auto can = [&](int type) {
    row("can", static_cast<unsigned long long>(type),
        s.CanSpawnBonusType(type) ? 1ULL : 0ULL);
  };
  const auto record = [&](int type) {
    s.RecordBonusSpawned(type);
    row("record", static_cast<unsigned long long>(type), 0);
  };
  const auto count = [&]() {
    const int n = s.StageBonusesSpawnedCount();
    max_bonus_count = std::max(max_bonus_count, n);
    row("count", 0, static_cast<unsigned long long>(n));
  };
  const auto reinit = [&]() { s.ReInit(); row("reinit", 0, 0); };

  // Milestone economy: exact binary fractions AND noisy decimal ones, so the
  // ledger pins both clean drains and accumulated rounding residue.
  add(150); add(850); milestone();
  add(999); add(1); milestone();
  add(500); add(500); milestone();
  add(125); add(125); add(250); add(500); milestone();
  add(5500); milestone();  // multi-drain: five milestones plus residue
  add(333); add(777); add(250); milestone();
  add(7); add(7); add(7); milestone();  // miss: count 0, fractional residue kept

  // Shield clamp, including the negative destruction sentinel (no lower clamp).
  shield(99); shield(-1); shield(3); shield(8);

  cheat(true);

  // Waves 1 -> 16: cross the first stage boundary (Cycle 1 -> 2). A fresh
  // cycle re-allows every bonus type; the recorded mask then locks duplicates.
  for (int w = 0; w < 15; ++w) inc();
  can(1); can(2); can(3); can(4); can(5);
  record(2); record(2);  // idempotent bit
  can(2); can(3);
  record(1); record(3); record(4); record(5);
  count();  // 5 of 5
  can(1);   // locked out inside the cycle

  // Waves 16 -> 31: the second boundary (Cycle 2 -> 3) resets the mask.
  for (int w = 0; w < 15; ++w) inc();
  if (s.CanSpawnBonusType(1)) saw_rollover_reallow = true;
  can(1); count();
  record(3); can(3); can(4);

  cheat(false);

  // ReInit, then the saturating Add: 200 first so the overflow guard trips,
  // then a near-UINT64_MAX top-up. Progress bits are pinned but never drained.
  reinit();
  add(200);
  add(UINT64_MAX - 100ULL);
  reinit();

  // Coverage contract (hard gate, same discipline as the starfield): a script
  // that silently lost the saturation, the multi-drain or the mask rollover
  // would hollow out the section, so failure aborts the dump instead of
  // recording it.
  if (!saw_saturation || !saw_multi_milestone || !saw_rollover_reallow ||
      max_bonus_count != GameRules::Combat::kMaxBonusesPerStage ||
      s.Value() != 0 || s.Level() != 1 ||
      s.Shield() != GameRules::Player::kInitialShields ||
      s.StageBonusMask() != 0 || s.Cheated()) {
    std::fprintf(stderr,
                 "dump_vectors: score coverage contract failed "
                 "(sat=%d multi=%d rollover=%d maxcount=%d value=%llu level=%d)\n",
                 saw_saturation ? 1 : 0, saw_multi_milestone ? 1 : 0,
                 saw_rollover_reallow ? 1 : 0, max_bonus_count,
                 static_cast<unsigned long long>(s.Value()), s.Level());
    std::exit(1);
  }
  std::printf("score.done %d %llu %d\n", op_index,
              static_cast<unsigned long long>(s.Value()), s.Level());
}

void DumpEntities() {
  // Explicit scale-1 world: the entity scenarios read the FormationGrid
  // globals and the Pix global-scale cache, and neither may depend on what a
  // previous section left behind.
  GameRules::Fleet::FormationGrid::Recompute(1.0f);
  Pix::SetGlobalScale(1.0f);
  const HeadlessTextures tex;
  DumpEntityPix();
  DumpEntityBullets(tex);
  DumpEntityWarp(tex);
  DumpEntityDecay(tex);
  DumpEntityKamikaze(tex);
  DumpEntityFleet(tex);
  DumpEntityPlayer(tex);
  DumpEntityExplosions();
}

// ---------------------------------------------------------------------------
// Fatia 9 -- level catalog: per-wave convoy tables + arrival splines.
// C++ identifies paths by POINTER; the rows pin identity by CONTENT instead:
// a registry of unique arrival pointers in first-encounter order (stage-1
// waves 1..15, then stage-2) assigns ids, and the Rust parity test matches
// them by comparing resampled points, not ordering. The resampler is
// sqrt+IEEE only, so every level.* row carries ZERO tolerance.
// ---------------------------------------------------------------------------
void DumpLevelData() {
  std::printf("level.total %zu\n", LevelData::GetTotalLevels());

  std::vector<const FlightPath*> registry;
  const auto path_id = [&registry](const FlightPath* p) -> int {
    for (size_t i = 0; i < registry.size(); ++i) {
      if (registry[i] == p) return static_cast<int>(i);
    }
    registry.push_back(p);
    return static_cast<int>(registry.size() - 1);
  };

  struct TableRow {
    int stage;
    int idx;
    int n;
  };
  struct ConvoyRow {
    int stage;
    int idx;
    int i;
    int wait;
    int tex;
    int pc;
    int mir;
    int spl;
    int sim;
    int dock;
    int pid;
  };
  std::vector<TableRow> tables;
  std::vector<ConvoyRow> convoys;
  int total_convoys = 0;

  for (int stage = 1; stage <= 2; ++stage) {
    for (int idx = 0; idx < 15; ++idx) {
      const int level = (stage == 1) ? idx + 1 : 16 + idx;
      const ConvoyData* cd = LevelData::GetConvoyData(static_cast<size_t>(level));
      int n = 0;
      while (cd[n].texture_id != TextureId::None) ++n;
      tables.push_back({stage, idx, n});
      total_convoys += n;
      for (int i = 0; i < n; ++i) {
        convoys.push_back({stage, idx, i, cd[i].wait, static_cast<int>(cd[i].texture_id),
                           cd[i].convoy_size_pc, cd[i].x_mirror ? 1 : 0, cd[i].split ? 1 : 0,
                           cd[i].simultaneous ? 1 : 0, static_cast<int>(cd[i].dock_align),
                           path_id(cd[i].arrival)});
      }
    }
  }
  for (const auto& t : tables) {
    std::printf("level.table %d %d %d\n", t.stage, t.idx, t.n);
  }
  for (const auto& c : convoys) {
    std::printf("level.convoy %d %d %d %d %d %d %d %d %d %d %d\n", c.stage, c.idx, c.i, c.wait,
                c.tex, c.pc, c.mir, c.spl, c.sim, c.dock, c.pid);
  }

  // Production GetConvoyData resolution across the whole first cycle, both
  // sides of every stage/wrap boundary, and deep repeating levels.
  std::vector<int> resolve_levels;
  resolve_levels.push_back(0);
  for (int l = 1; l <= 30; ++l) resolve_levels.push_back(l);
  for (int l : {31, 45, 46, 60, 75, 76, 90, 105, 999}) resolve_levels.push_back(l);
  for (const int level : resolve_levels) {
    const int effective = (level == 0) ? 1 : level;
    const int idx = (effective - 1) % 15;
    const int stage = GameRules::Progression::WaveToStage(effective);
    const int tbl = (stage >= 2) ? 2 : 1;
    const ConvoyData* cd = LevelData::GetConvoyData(static_cast<size_t>(level));
    int n = 0;
    while (cd[n].texture_id != TextureId::None) ++n;
    std::printf("level.resolve %d %d %d %d %d %d %d\n", level, stage, idx, tbl, n, cd[0].wait,
                static_cast<int>(cd[0].texture_id));
  }

  // Arrival splines: every resampled point, bit-exact.
  size_t total_pts = 0;
  for (size_t id = 0; id < registry.size(); ++id) {
    const FlightPath& fp = *registry[id];
    std::printf("level.path %zu %zu\n", id, fp.Size());
    total_pts += fp.Size();
    for (size_t j = 0; j < fp.Size(); ++j) {
      std::printf("level.pt %zu %zu %08x %08x\n", id, j, FloatBits(fp[j].x), FloatBits(fp[j].y));
    }
  }

  // Hard gates: a truncated catalog would silently weaken every future
  // replay that rebuilds waves from it.
  if (registry.size() < 20 || tables.size() != 30 || total_convoys < 100 || total_pts < 3000) {
    std::fprintf(stderr,
                 "DumpLevelData gates failed: paths=%zu tables=%zu convoys=%d pts=%zu\n",
                 registry.size(), tables.size(), total_convoys, total_pts);
    std::exit(1);
  }
  std::printf("level.done %zu %zu %d %zu\n", registry.size(), total_pts, total_convoys,
              tables.size());
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
  DumpEntities();
  DumpStarfield();
  DumpScore();
  DumpLevelData();
  return 0;
}
