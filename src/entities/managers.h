#ifndef MANAGERS_H
#define MANAGERS_H

#include <array>
#include <deque>
#include <limits>
#include <memory>
#include <random>
#include <vector>

#include "actors.h"
#include "constants.h"
#include "stage_catalog.h"
#include "embedded_assets.h"
#include "explosion.h"
#include "game_object.h"
#include "path.h"
#include "random_stream.h"

class Score;
class SoundManager;

struct ConvoyData {
  int wait;
  TextureId texture_id;
  int convoy_size_pc;
  const FlightPath* arrival;
  bool x_mirror;
  bool split;
  bool simultaneous{false};
  int8_t dock_align{0};  // 0 = Center, 1 = Left Flank, 2 = Right Flank

  [[nodiscard]] int ConvoySize() const;
};

struct ExhaustParticle {
  Vec2f pos{0.0f, 0.0f};
  Vec2f vel{0.0f, 0.0f};
  uint8_t r{255}, g{180}, b{40};
  int life{0};
  int max_life{10};
  bool active{false};

  void Move() noexcept {
    if (active) {
      pos += vel;
      vel *= 0.94f;
      if (--life <= 0) active = false;
    }
  }

  void Draw() const;
};

struct ProjectileSlot {
  const Pix* pix{nullptr};
  Coord pos{0, 0};
  Coord speed{0, 0};
  float fx{0.0f}, fy{0.0f};
  float prev_fx{0.0f}, prev_fy{0.0f};
  bool active{false};
  bool relativistic{false};
  RandomStream rng_{};
  bool radioactive{false};
  int wave_phase{0};

  bool is_turning_bomb{false};
  bool turn_completed{false};
  int turn_trigger_y{0};
  int turn_timer{0};
  float bomb_render_angle{0.0f};
  float deflection_angle_deg{0.0f};
  float speed_magnitude{0.0f};

  uint8_t engine_r{210};
  uint8_t engine_g{60};
  uint8_t engine_b{255};

  // Pre-calculated integer hitbox extents: eliminates Pix::Height() & Scale() queries in hot collision loop
  int hitbox_w{0};
  int hitbox_h{0};

  void Move(std::array<ExhaustParticle, 128>& exhaust_pool,
            std::array<uint16_t, 128>& active_exhaust,
            int& active_exhaust_count) noexcept;
  void SeedRandom(std::uint32_t seed) noexcept { rng_.Seed(seed); }
  [[nodiscard]] bool Out() const noexcept;
  [[nodiscard]] bool Collide(const simulation::GameObject& other) const noexcept;
  void Draw(float alpha = 1.0f) const;
  void OnResize(float rx, float ry) noexcept;
};

class BulletsManager {
  static constexpr size_t MAX_BULLETS = GameRules::Combat::kMaxProjectiles;
  static constexpr size_t MAX_EXHAUST = GameRules::Combat::kMaxExhaustParticles;

  std::array<ProjectileSlot, MAX_BULLETS> pool_{};
  std::array<uint16_t, MAX_BULLETS> active_ids_{};
  std::array<ExhaustParticle, MAX_EXHAUST> exhaust_pool_{};
  std::array<uint16_t, MAX_EXHAUST> active_exhaust_ids_{};
  RandomStream rng_{0x5EED0001u};
  int active_count_{0};
  int active_exhaust_count_{0};
  int cached_min_y_{std::numeric_limits<int>::max()};
  int cached_max_y_{std::numeric_limits<int>::min()};

 public:
  BulletsManager() = default;
  explicit BulletsManager(std::uint32_t seed) noexcept : rng_(seed) {}
  ~BulletsManager() = default;
  void SeedRandom(std::uint32_t seed) noexcept { rng_.Seed(seed); }

  void Move();
  void Draw(float alpha = 1.0f) const;
  void Add(const Pix* pix, Coord pos, Coord speed, bool relativistic = false,
           bool is_turning = false, int trigger_y = 0, float deflection_deg = 0.0f,
           bool radioactive = false, uint8_t eng_r = 210, uint8_t eng_g = 60,
           uint8_t eng_b = 255);
  int DoCollisions(const simulation::GameObject& other, int max);
  [[nodiscard]] bool HasBombNear(Coord pos, int min_dist_px = 52) const;
  [[nodiscard]] bool HasBulletNear(Coord pos, float distance_threshold, Coord* out_bullet_pos = nullptr) const noexcept;
  [[nodiscard]] bool FindThreateningBullet(Coord pos, float alien_half_w, float trigger_dist, uint16_t* out_bullet_idx = nullptr) const noexcept;
  void ConsumeBullet(uint16_t bullet_idx) noexcept;
  bool ConsumeBulletNear(Coord pos, float distance_threshold) noexcept;
  [[nodiscard]] bool WouldTrapPlayer(Coord spawn_pos, Coord speed, int player_y,
                                     int safe_corridor, int player_x, int window_w) const;
  void OnResize(float rx, float ry);
  [[nodiscard]] int Nb() const noexcept { return active_count_; }

  void GetYBounds(int& out_min_y, int& out_max_y) const noexcept {
    out_min_y = cached_min_y_;
    out_max_y = cached_max_y_;
  }
};

struct BonusSlot {
  const Pix* pix{nullptr};
  Coord pos{0, 0};
  Coord speed{0, 1};
  Bonus::bonus_t type{Bonus::none};
  bool active{false};

  void Move() noexcept {
    if (active) pos += speed;
  }
  [[nodiscard]] bool Out() const noexcept;
  [[nodiscard]] bool Collide(const simulation::GameObject& other) const noexcept;
  void Draw() const;
  void OnResize(float rx, float ry) noexcept;
};

class BonusManager {
  static constexpr size_t MAX_BONUSES = GameRules::Combat::kMaxBonuses;
  std::array<BonusSlot, MAX_BONUSES> pool_{};
  std::array<uint16_t, MAX_BONUSES> active_ids_{};
  int active_count_{0};

 public:
  BonusManager() = default;
  ~BonusManager() = default;

  void Move();
  void Draw() const;
  void Add(Coord pos, Bonus::bonus_t type);
  Bonus::bonus_t GetBonusCollision(const simulation::GameObject& other);
  void OnResize(float rx, float ry);
};

struct BulletCollisionSummary {
  int total_hits{0};
  int regular_hits{0};
  int kamikaze_hits{0};
  uint64_t score_earned{0};

  constexpr operator int() const noexcept { return total_hits; }
};

class Player;

class AliensManager {
  BulletsManager* const bombs_manager_;
  BulletsManager* const bullets_manager_;
  BonusManager* const bonus_manager_;
  ExplosionsManager* const explosions_manager_;
  Score* const score_{nullptr};
  SoundManager* const audio_{nullptr};
  const int level_number_;
  const GameRules::StageProfile stage_profile_;
  mutable RandomStream rng_{0x5EED0002u};
  const ConvoyData* const convoys_;
  const int nconvoys_;
  int max_convoy_size_;
  int convoy_idx_;
  int convoy_alien_idx_;
  const float speed_;
  Coord base_cruise_;
  int base_cruise_speed_;

  enum FleetState { spawning_convoys, active_combat };
  FleetState fleet_state_{spawning_convoys};

  int next_creation_wait_;
  mutable int fire_cooldown_{0};

  int bonus_wait_;
  bool bonus_allowed_this_level_{false};
  bool bonus_spawned_this_level_{false};

  std::deque<FlightPath> random_paths_;
  bool random_wanderers_spawned_{false};
  int original_max_convoy_size_{0};
  int alien4_spawn_count_{0};
  const bool wanderers_allowed_cycle_{false};

  mutable int turning_bombs_remaining_{0};
  mutable int turning_spacing_cooldown_{0};

  const Player* player_{nullptr};

  int min_cruise_x_{0};
  int max_cruise_x_{0};
  void UpdateCruiseBounds() noexcept;

  // Single Source of Truth for fleet occupancy bitset (Deep DRY)
  [[nodiscard]] std::array<std::uint16_t, GameRules::Fleet::FormationGrid::kMaxGridRows>
  BuildOccupancyBitset(const Alien* exclude_alien) const noexcept;

  void SpawnRandomWanderers();
  [[nodiscard]] bool AreAllAliensDocked() const noexcept;
  [[nodiscard]] Bonus::bonus_t RollSmartBonusType() const noexcept;

  using AliensCtn = std::vector<std::unique_ptr<Alien>>;
  AliensCtn aliens_;

 public:
  AliensManager(BulletsManager* bombs_manager, BulletsManager* bullets_manager,
                BonusManager* bonus_manager,
                ExplosionsManager* explosions_manager,
                Score* score, SoundManager* audio,
                int level_number, const ConvoyData* convoysdata, int max_level,
                std::uint32_t random_seed = 0x5EED0002u);
  ~AliensManager() = default;

  void SeedRandom(std::uint32_t seed) noexcept { rng_.Seed(seed); }
  void SetPlayer(const Player* p) noexcept { player_ = p; }

  void Move();
  void Draw(float alpha = 1.0f) const;
  void Fire(Coord player_pos = Coord(-1, -1)) const;
  void OnResize(float rx, float ry);
  void NukeAll();
  BulletCollisionSummary DoBulletsCollisions();
  [[nodiscard]] bool CheckKamikazeProximity(Coord player_pos, float lethal_radius);
  [[nodiscard]] bool Finished() const noexcept {
    return fleet_state_ == active_combat && aliens_.empty();
  }

 private:
  [[nodiscard]] int GetMaxConvoySize() const;
  void Creation();
  void CreateBonusMaybe(Coord pos);
};

class Player {
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  simulation::GameObject object_{};
  BulletsManager* const bullets_manager_;
  BulletsManager* const bombs_manager_;
  BonusManager* const bonus_manager_;
  ExplosionsManager* const explosions_manager_;
  const int below_y_;
  const Config* config_{nullptr};
  SoundManager* audio_{nullptr};

  int shield_{GameRules::Player::kInitialShields};
  int speed_level_{0};
  int fire_level_{0};
  int fire_wait_{0};
  int fire_interval_{static_cast<int>(GameRules::Player::kBaseFireIntervalFrames)};
  int multi_fire_{1};

  int hit_timer_{0};
  float fx_{0.0f};
  float prev_fx_{0.0f};

 public:
  Player(BulletsManager* bullets_manager, BulletsManager* bombs_manager,
         BonusManager* bonus_manager, ExplosionsManager* explosions_manager,
         int below_y, const Config* config = nullptr, SoundManager* audio = nullptr);

  [[nodiscard]] Coord Position() const noexcept { return object_.Position(); }
  [[nodiscard]] int Width() const noexcept { return object_.Width(); }
  [[nodiscard]] int Height() const noexcept { return object_.Height(); }
  [[nodiscard]] Coord Dim() const noexcept { return object_.Dim(); }
  [[nodiscard]] const simulation::GameObject& GetGameObject() const noexcept { return object_; }

  void Move(int direction);
  void Fire();
  void OnResize(float rx = 1.0f, float ry = 1.0f) noexcept;
  void DoBombsCollisions();
  void DoBonusCollisions(AliensManager* aliens_mgr = nullptr);
  void Draw(float angle = 0.0f) const;
  void DrawInterpolated(float alpha, float angle = 0.0f) const;

  [[nodiscard]] int Shield() const noexcept { return shield_; }
  void ExtraFire() noexcept;
  void ExtraMulti() noexcept;
  void ExtraShield() noexcept;
  void ExtraSpeed() noexcept;

  [[nodiscard]] bool IsFireMaxed() const noexcept { return fire_level_ >= GameRules::Player::kPlayerMaxFireLevel; }
  [[nodiscard]] bool IsMultiMaxed() const noexcept { return multi_fire_ >= GameRules::Player::kPlayerMaxMultiShots; }
  [[nodiscard]] bool IsSpeedMaxed() const noexcept { return speed_level_ >= GameRules::Player::kPlayerMaxSpeedLevel; }
  [[nodiscard]] bool IsShieldGated() const noexcept { return shield_ > GameRules::Player::kPlayerShieldGateThreshold; }
  [[nodiscard]] bool IsShieldMaxed() const noexcept { return shield_ >= GameRules::Player::kPlayerMaxShield; }

 private:
  void MoveTo(Coord to) noexcept { object_.transform.position = to; }
  void UpdateAABB() noexcept {
    object_.collider.aabb.half_extents = Vec2f(static_cast<float>(Width()) * 0.5f, static_cast<float>(Height()) * 0.5f);
  }
  [[nodiscard]] int PosY() const;
};

#endif  // MANAGERS_H
