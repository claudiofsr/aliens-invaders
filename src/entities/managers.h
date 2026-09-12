#ifndef MANAGERS_H
#define MANAGERS_H

#include <array>
#include <memory>
#include <random>
#include <vector>

#include "constants.h"
#include "embedded_assets.h"
#include "explosion.h"
#include "game_rules.h"
#include "path.h"
#include "sprites.h"

struct ConvoyData {
  int wait;
  SpriteId sprite_id;
  int convoy_size_pc;
  const FlightPath* arrival;
  bool x_mirror;
  bool split;

  [[nodiscard]] int ConvoySize() const;
};

// Compact Rocket Thruster Particle
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
  bool active{false};
  bool relativistic{false};
  bool radioactive{false};
  int wave_phase{0};

  // Vector-Redirect Alien Rocket Properties
  bool is_turning_bomb{false};
  bool turn_completed{false};
  int turn_trigger_y{0};
  int turn_timer{0};
  float bomb_render_angle{0.0f};    // Nose locked strictly to flight vector
  float deflection_angle_deg{0.0f}; // Trajectory angle [-15°, +15°]
  float speed_magnitude{0.0f};

  // Engine Thruster Signature
  uint8_t engine_r{210};
  uint8_t engine_g{60};
  uint8_t engine_b{255};

  void Move(std::array<ExhaustParticle, 128>& exhaust_pool) noexcept;
  [[nodiscard]] bool Out() const noexcept;
  [[nodiscard]] bool Collide(const Sprite& other) const noexcept;
  void Draw() const;
  void OnResize(float rx, float ry) noexcept;
};

class BulletsManager {
  static constexpr size_t MAX_BULLETS = 512;
  static constexpr size_t MAX_EXHAUST = 128;

  std::array<ProjectileSlot, MAX_BULLETS> pool_{};
  std::array<ExhaustParticle, MAX_EXHAUST> exhaust_pool_{};

 public:
  BulletsManager() = default;
  ~BulletsManager() = default;

  void Move();
  void Draw() const;
  void Add(const Pix* pix, Coord pos, Coord speed, bool relativistic = false,
           bool is_turning = false, int trigger_y = 0, float deflection_deg = 0.0f,
           bool radioactive = false, uint8_t eng_r = 210, uint8_t eng_g = 60,
           uint8_t eng_b = 255);
  int DoCollisions(const Sprite& other, int max);
  [[nodiscard]] bool HasBombNear(Coord pos, int min_dist_px = 52) const;
  [[nodiscard]] bool WouldTrapPlayer(Coord spawn_pos, Coord speed, int player_y,
                                     int safe_corridor, int player_x, int window_w) const;
  void OnResize(float rx, float ry);
  [[nodiscard]] int Nb() const noexcept;
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
  [[nodiscard]] bool Collide(const Sprite& other) const noexcept;
  void Draw() const;
  void OnResize(float rx, float ry) noexcept;
};

class BonusManager {
  static constexpr size_t MAX_BONUSES = 64;
  std::array<BonusSlot, MAX_BONUSES> pool_{};

 public:
  BonusManager() = default;
  ~BonusManager() = default;

  void Move();
  void Draw() const;
  void Add(Coord pos, Bonus::bonus_t type);
  Bonus::bonus_t GetBonusCollision(const Sprite& other);
  void OnResize(float rx, float ry);
};

class ExplosionsManager {
  static constexpr size_t MAX_EXPLOSIONS = 256;
  using ExplosionsCtn = std::vector<std::unique_ptr<Explosion>>;
  ExplosionsCtn explosions_;

 public:
  ExplosionsManager();
  ~ExplosionsManager() = default;

  void Move();
  void Draw() const;
  void Add(Coord pos, Coord speed, uint8_t r, uint8_t g, uint8_t b,
           int duration = 24);
};

class Player; // Forward declaration

class AliensManager {
  BulletsManager* const bombs_manager_;
  BulletsManager* const bullets_manager_;
  BonusManager* const bonus_manager_;
  ExplosionsManager* const explosions_manager_;
  const int level_number_;
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

  // Loot parameters: 50% chance per stage, max 1 bonus per stage
  int bonus_wait_;
  bool bonus_allowed_this_level_{false};
  bool bonus_spawned_this_level_{false};

  // Cycle 2+ Wanderer Fleet
  std::vector<FlightPath> random_paths_;
  static constexpr int kRandomWanderers = 4;
  bool random_wanderers_spawned_{false};
  int original_max_convoy_size_{0};
  const bool wanderers_allowed_cycle_{false};

  // 2 to 10 vector-redirect missiles randomly distributed throughout the stage
  mutable int turning_bombs_remaining_{0};
  mutable int turning_spacing_cooldown_{0};

  const Player* player_{nullptr};

  void SpawnRandomWanderers();
  [[nodiscard]] bool AreAllAliensDocked() const noexcept;
  [[nodiscard]] Bonus::bonus_t RollSmartBonusType() const noexcept;

  using AliensCtn = std::vector<std::unique_ptr<Alien>>;
  AliensCtn aliens_;

 public:
  AliensManager(BulletsManager* bombs_manager, BulletsManager* bullets_manager,
                BonusManager* bonus_manager,
                ExplosionsManager* explosions_manager, int level_number,
                const ConvoyData* convoysdata, int max_level);
  ~AliensManager() = default;

  void SetPlayer(const Player* p) noexcept { player_ = p; }

  void Move();
  void Draw() const;
  void Fire(Coord player_pos = Coord(-1, -1)) const;
  void OnResize(float rx, float ry);
  void NukeAll();
  int DoBulletsCollisions();
  [[nodiscard]] bool Finished() const noexcept {
    return fleet_state_ == active_combat && aliens_.empty();
  }

 private:
  [[nodiscard]] int GetMaxConvoySize() const;
  void Creation();
  void CreateBonusMaybe(Coord pos);
};

class Player : public Sprite {
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  BulletsManager* const bullets_manager_;
  BulletsManager* const bombs_manager_;
  BonusManager* const bonus_manager_;
  ExplosionsManager* const explosions_manager_;
  const int below_y_;

  // Pure domain-driven kinetics
  int shield_{3};
  int speed_level_{0}; // 0 to 2 (max 120%)
  int fire_level_{0};  // 0 to 2 (max 120%)
  int fire_wait_{0};
  int fire_interval_{static_cast<int>(GameRules::kPlayerBaseFireInterval)};
  int multi_fire_{1};  // 1 to 3 simultaneous shots max

  int hit_timer_{0};
  static constexpr int kMaxHitTimer = 42;

 public:
  Player(BulletsManager* bullets_manager, BulletsManager* bombs_manager,
         BonusManager* bonus_manager, ExplosionsManager* explosions_manager,
         int below_y);
  void Move(int direction);
  void Fire();
  void OnResize(float rx = 1.0f, float ry = 1.0f) noexcept override;
  void DoBombsCollisions();
  void DoBonusCollisions(AliensManager* aliens_mgr = nullptr);
  void Draw(float angle = 0.0f) const override;

  [[nodiscard]] int Shield() const noexcept { return shield_; }
  void ExtraFire() noexcept;
  void ExtraMulti() noexcept;
  void ExtraShield() noexcept;
  void ExtraSpeed() noexcept;

  // Smart Loot State Queries
  [[nodiscard]] bool IsFireMaxed() const noexcept { return fire_level_ >= GameRules::kPlayerMaxFireLevel; }
  [[nodiscard]] bool IsMultiMaxed() const noexcept { return multi_fire_ >= GameRules::kPlayerMaxMultiShots; }
  [[nodiscard]] bool IsSpeedMaxed() const noexcept { return speed_level_ >= GameRules::kPlayerMaxSpeedLevel; }
  [[nodiscard]] bool IsShieldGated() const noexcept { return shield_ > GameRules::kPlayerShieldGateThreshold; }
  [[nodiscard]] bool IsShieldMaxed() const noexcept { return shield_ >= GameRules::kPlayerMaxShield; }

 private:
  [[nodiscard]] int PosY() const;
};

#endif  // MANAGERS_H
