#ifndef SPRITES_H
#define SPRITES_H

#include <vector>
#include <cmath>

#include "gfxinterface.h"
#include "math_types.h"
#include "path.h"
#include "random_stream.h"
#include "pix.h"

class Sprite {
 protected:
  const Pix* pix_{nullptr};
  Coord pos_{};
  AABB aabb_{};

 public:
  Sprite(const Pix* pix, Coord pos);
  virtual ~Sprite() = default;

  [[nodiscard]] Coord Position() const noexcept { return pos_; }
  [[nodiscard]] int Width() const noexcept { return pix_ ? pix_->Width() : 0; }
  [[nodiscard]] int Height() const noexcept { return pix_ ? pix_->Height() : 0; }
  [[nodiscard]] Coord Dim() const noexcept { return pix_ ? pix_->Dim() : Coord(0, 0); }
  [[nodiscard]] const AABB& Collider() const noexcept { return aabb_; }

  virtual void Draw(float angle = 0.0f) const {
    if (pix_) pix_->Draw(pos_, angle);
  }

  [[nodiscard]] bool Collide(const Sprite& other) const noexcept {
    return aabb_.Intersects(pos_.ToVec2f(), other.pos_.ToVec2f(), other.aabb_, 0.65f);
  }

  virtual void OnResize(float rx, float ry) noexcept {
    pos_.x = static_cast<int32_t>(std::lround(static_cast<double>(pos_.x) * static_cast<double>(rx)));
    pos_.y = static_cast<int32_t>(std::lround(static_cast<double>(pos_.y) * static_cast<double>(ry)));
    UpdateAABB();
  }

 protected:
  void MoveTo(Coord to) noexcept {
    pos_ = to;
  }
  void UpdateAABB() noexcept {
    aabb_.half_extents = Vec2f(static_cast<float>(Width()) * 0.5f, static_cast<float>(Height()) * 0.5f);
  }
};

class Projectile : public Sprite {
  const Coord speed_;

 public:
  Projectile(const Pix* pix, Coord pos, Coord speed)
      : Sprite(pix, pos), speed_(speed) {}
  void Move() noexcept { MoveTo(Position() + speed_); }
  [[nodiscard]] bool Out() const noexcept;
  [[nodiscard]] Coord Speed() const noexcept { return speed_; }
};

class Bonus : public Projectile {
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
  const bonus_t type_;

 public:
  Bonus(Coord pos, bonus_t type);
  [[nodiscard]] bonus_t Type() const noexcept { return type_; }
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
  void NextPositionF(float from_x, float from_y, float velocity, float& out_x,
                     float& out_y);
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

class Alien : public Sprite {
  Trajectory trajectory_;
  const float speed_;
  Transform2D transform_;
  SpriteId sprite_id_{SpriteId::None};
  float angle_{0.0f};
  float dir_x_{0.0f};
  float dir_y_{1.0f};
  float spin_angle_{0.0f};
  int spin_timer_{0};
  static constexpr int kSpinFrames = 46;
  bool has_spun_{false};

  int attack_wait_timer_{0};

  bool is_kamikaze_{false};
  bool is_wanderer_{false};
  mutable float kamikaze_phase_{0.0f};

 public:
  Alien(const Pix* pix, const Trajectory& trajectory, float speed, SpriteId sprite_id = SpriteId::None);
  [[nodiscard]] SpriteId GetSpriteId() const noexcept { return sprite_id_; }
  void Move();
  void Draw(float extra_angle = 0.0f) const override;
  void DrawInterpolated(float alpha, float extra_angle = 0.0f) const;
  void OnResize(float rx, float ry) noexcept override;
  [[nodiscard]] float Angle() const noexcept { return angle_ + spin_angle_; }
  [[nodiscard]] Coord CannonPosition() const;
  [[nodiscard]] Trajectory::stage_t Stage() const noexcept {
    return trajectory_.Stage();
  }
  void BuildAttack() { trajectory_.BuildAttack(Position()); }
  [[nodiscard]] int UniformInt(int min_value, int max_value) noexcept {
    return trajectory_.UniformInt(min_value, max_value);
  }
  void BuildKamikazeDive(Coord target_player) {
    trajectory_.BuildKamikazeDive(Position(), target_player);
  }
  void ForceCruise(Coord target);

  void SetAttackTimer(int frames) noexcept { attack_wait_timer_ = frames; }
  void DecrementAttackTimer() noexcept {
    if (attack_wait_timer_ > 0) --attack_wait_timer_;
  }
  [[nodiscard]] bool ShouldLaunchAttack() const noexcept {
    return (trajectory_.Stage() == Trajectory::cruising) && (attack_wait_timer_ == 0);
  }

  void SetKamikaze(bool k) noexcept { is_kamikaze_ = k; }
  [[nodiscard]] bool IsKamikaze() const noexcept { return is_kamikaze_; }
  void SetWanderer(bool w) noexcept { is_wanderer_ = w; }
  [[nodiscard]] bool IsWanderer() const noexcept { return is_wanderer_; }
};

#endif  // SPRITES_H
