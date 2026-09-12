#ifndef SPRITES_H
#define SPRITES_H

#include <vector>
#include <cmath>
#include <cstdlib>

#include "gfxinterface.h"
#include "path.h"
#include "pix.h"

class Sprite {
 protected:
  const Pix* pix_{nullptr};
  Coord pos_{};

 public:
  Sprite(const Pix* pix, Coord pos);
  virtual ~Sprite() = default;

  [[nodiscard]] Coord Position() const noexcept { return pos_; }
  [[nodiscard]] int Width() const noexcept { return pix_ ? pix_->Width() : 0; }
  [[nodiscard]] int Height() const noexcept {
    return pix_ ? pix_->Height() : 0;
  }
  [[nodiscard]] Coord Dim() const noexcept {
    return pix_ ? pix_->Dim() : Coord(0, 0);
  }
  virtual void Draw(float angle = 0.0f) const {
    if (pix_) pix_->Draw(pos_, angle);
  }
  [[nodiscard]] bool Collide(const Sprite& other) const noexcept;
  virtual void OnResize(float rx, float ry) noexcept {
    pos_.x = static_cast<short>(std::round(pos_.x * rx));
    pos_.y = static_cast<short>(std::round(pos_.y * ry));
  }

 protected:
  void MoveTo(Coord to) noexcept { pos_ = to; }
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
  short grid_col_{0};
  short grid_row_{0};
  std::vector<Vec2f> attack_;
  size_t attack_idx_;

 public:
  Trajectory(const FlightPath* arrival, bool mirrored, const Coord& base_cruise,
             short grid_col, short grid_row);
  [[nodiscard]] stage_t Stage() const noexcept { return stage_; }
  void NextPositionF(float from_x, float from_y, float velocity, float& out_x,
                     float& out_y);
  [[nodiscard]] Coord InitPosition() const;
  [[nodiscard]] Coord CruiseTarget() const noexcept;
  void BuildAttack(Coord from);
  void ForceCruise() noexcept {
    stage_ = cruising;
    arrival_idx_ = arrival_path_ ? arrival_path_->Size() : 0;
  }
};

class Alien : public Sprite {
  Trajectory trajectory_;
  const float speed_;
  float fx_{0.0f};
  float fy_{0.0f};
  float angle_{0.0f};
  float dir_x_{0.0f};
  float dir_y_{1.0f};
  float spin_angle_{0.0f};
  int spin_timer_{0};
  static constexpr int kSpinFrames = 46;
  bool has_spun_{false};

  // Independent per-alien attack timer
  int attack_wait_timer_{0};

 public:
  Alien(const Pix* pix, const Trajectory& trajectory, float speed);
  void Move();
  void Draw(float extra_angle = 0.0f) const override;
  void OnResize(float rx, float ry) noexcept override;
  [[nodiscard]] float Angle() const noexcept { return angle_ + spin_angle_; }
  [[nodiscard]] Coord CannonPosition() const;
  [[nodiscard]] Trajectory::stage_t Stage() const noexcept {
    return trajectory_.Stage();
  }
  void BuildAttack() { trajectory_.BuildAttack(Position()); }
  void ForceCruise(Coord target);

  // Independent per-alien attack timing
  void SetAttackTimer(int frames) noexcept { attack_wait_timer_ = frames; }
  void DecrementAttackTimer() noexcept {
    if (attack_wait_timer_ > 0) --attack_wait_timer_;
  }
  [[nodiscard]] bool ShouldLaunchAttack() const noexcept {
    return (trajectory_.Stage() == Trajectory::cruising) && (attack_wait_timer_ == 0);
  }
  void ReassignPostDockAttackTimer(int max_wait_frames) noexcept {
    attack_wait_timer_ = std::rand() % (std::max(1, max_wait_frames) + 1);
  }
};

#endif  // SPRITES_H
