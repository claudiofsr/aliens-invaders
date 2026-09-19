#ifndef ACTORS_H
#define ACTORS_H

#include <vector>
#include <cmath>

#include "game_object.h"
#include "constants.h"
#include "gfxinterface.h"
#include "math_types.h"
#include "path.h"
#include "pix.h"
#include "random_stream.h"

/**
 * @class Bonus
 * @brief Battlefield collectible item composing simulation::GameObject.
 */
class Bonus {
 public:
  enum bonus_t {
    none = 0,
    extra_speed = 1,
    extra_fire = 2,
    extra_shield = 3,
    extra_multi = 4,
    extra_nuke = 5
  };

 private:
  simulation::GameObject object_{};
  bonus_t type_{none};

 public:
  Bonus(Coord pos, bonus_t type);

  [[nodiscard]] bonus_t Type() const noexcept { return type_; }
  [[nodiscard]] Coord Position() const noexcept { return object_.Position(); }
  [[nodiscard]] int Width() const noexcept { return object_.Width(); }
  [[nodiscard]] int Height() const noexcept { return object_.Height(); }
  [[nodiscard]] Coord Dim() const noexcept { return object_.Dim(); }
  [[nodiscard]] const simulation::Collider& Collider() const noexcept { return object_.collider; }
  [[nodiscard]] const simulation::GameObject& GetGameObject() const noexcept { return object_; }

  void Move() noexcept { object_.Update(); }
  void Draw() const { object_.Draw(); }
  [[nodiscard]] bool Out() const noexcept;
  [[nodiscard]] bool Collide(const simulation::GameObject& other) const noexcept {
    return object_.Intersects(other, 0.65f);
  }
};

class Trajectory {
 public:
  enum stage_t { arriving, joining, cruising, attacking };

 private:
  stage_t stage_;
  const FlightPath* arrival_path_;
  size_t arrival_idx_;
  const bool mirrored_;
  const Coord& base_cruise_;
  int grid_col_{0};
  int grid_row_{0};
  std::vector<Vec2f> attack_;
  RandomStream rng_;
  size_t attack_idx_;

 public:
  Trajectory(const FlightPath* arrival, bool mirrored, const Coord& base_cruise,
             int grid_col, int grid_row, std::uint32_t random_seed = 0x9E3779B9u);
  [[nodiscard]] stage_t Stage() const noexcept { return stage_; }
  void NextPositionF(float from_x, float from_y, float velocity, float& out_x, float& out_y);
  [[nodiscard]] Coord InitPosition() const;
  [[nodiscard]] Coord CruiseTarget() const noexcept;
  void BuildAttack(Coord from);
  void BuildKamikazeDive(Coord from, Coord target_player);
  [[nodiscard]] int UniformInt(int min_value, int max_value) noexcept { return rng_.UniformInt(min_value, max_value); }
  [[nodiscard]] bool Chance(int numerator, int denominator) noexcept { return rng_.Chance(numerator, denominator); }
  void ForceCruise() noexcept {
    stage_ = cruising;
    arrival_idx_ = arrival_path_ ? arrival_path_->Size() : 0;
  }
};

/**
 * @class Alien
 * @brief Armada combat vessel composing simulation::GameObject.
 */
class Alien {
  simulation::GameObject object_{};
  Trajectory trajectory_;
  const float speed_;
  Transform2D transform_;
  TextureId texture_id_{TextureId::None};
  float angle_{0.0f};
  float dir_x_{0.0f};
  float dir_y_{1.0f};
  float spin_angle_{0.0f};
  int spin_timer_{0};
  bool has_spun_{false};

  int attack_wait_timer_{0};

  bool is_kamikaze_{false};
  bool is_wanderer_{false};
  bool has_electrosphere_{true};
  float kamikaze_phase_{0.0f};
  float electron_phase_{0.0f};

 public:
  Alien(const Pix* pix, const Trajectory& trajectory, float speed, TextureId texture_id = TextureId::None);

  [[nodiscard]] TextureId GetTextureId() const noexcept { return texture_id_; }
  [[nodiscard]] Coord Position() const noexcept { return object_.Position(); }
  [[nodiscard]] int Width() const noexcept { return object_.Width(); }
  [[nodiscard]] int Height() const noexcept { return object_.Height(); }
  [[nodiscard]] Coord Dim() const noexcept { return object_.Dim(); }
  [[nodiscard]] const simulation::GameObject& GetGameObject() const noexcept { return object_; }

  void Move();
  void Draw(float extra_angle = 0.0f) const;
  void DrawInterpolated(float alpha, float extra_angle = 0.0f) const;
  void OnResize(float rx, float ry) noexcept;
  [[nodiscard]] float Angle() const noexcept { return angle_ + spin_angle_; }
  [[nodiscard]] Coord CannonPosition() const;
  [[nodiscard]] Trajectory::stage_t Stage() const noexcept { return trajectory_.Stage(); }
  void BuildAttack() { trajectory_.BuildAttack(Position()); }
  [[nodiscard]] int UniformInt(int min_value, int max_value) noexcept { return trajectory_.UniformInt(min_value, max_value); }
  [[nodiscard]] bool Chance(int numerator, int denominator) noexcept { return trajectory_.Chance(numerator, denominator); }
  void BuildKamikazeDive(Coord target_player) { trajectory_.BuildKamikazeDive(Position(), target_player); }
  void ForceCruise(Coord target);

  void SetAttackTimer(int frames) noexcept { attack_wait_timer_ = frames; }
  void DecrementAttackTimer() noexcept { if (attack_wait_timer_ > 0) --attack_wait_timer_; }
  [[nodiscard]] bool ShouldLaunchAttack() const noexcept { return (trajectory_.Stage() == Trajectory::cruising) && (attack_wait_timer_ == 0); }

  void SetKamikaze(bool k) noexcept { is_kamikaze_ = k; }
  [[nodiscard]] bool IsKamikaze() const noexcept { return is_kamikaze_; }
  void SetWanderer(bool w) noexcept { is_wanderer_ = w; }
  [[nodiscard]] bool IsWanderer() const noexcept { return is_wanderer_; }

  void SetElectrosphere(bool enabled) noexcept { has_electrosphere_ = enabled; }
  [[nodiscard]] bool HasElectrosphere() const noexcept { return has_electrosphere_; }

 private:
  void MoveTo(Coord to) noexcept { object_.transform.position = to; }
  void UpdateAABB() noexcept {
    object_.collider.aabb.half_extents = Vec2f(static_cast<float>(Width()) * 0.5f, static_cast<float>(Height()) * 0.5f);
  }
};

#endif  // ACTORS_H
