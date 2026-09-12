#include "sprites.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include "config.h"
#include "constants.h"
#include "embedded_assets.h"
#include "sdl_window.h"

Sprite::Sprite(const Pix* pix, Coord pos) : pix_(pix), pos_(pos) {
  if (!pix) throw std::invalid_argument("Null pixmap for sprite");
}

bool Sprite::Collide(const Sprite& other) const noexcept {
  const int hw1 = (Width() * 65) / 100;
  const int hh1 = (Height() * 65) / 100;
  const int hw2 = (other.Width() * 65) / 100;
  const int hh2 = (other.Height() * 65) / 100;

  return std::abs(pos_.x - other.pos_.x) * 2 < (hw1 + hw2) &&
         std::abs(pos_.y - other.pos_.y) * 2 < (hh1 + hh2);
}

bool Projectile::Out() const noexcept {
  return Position().y + Height() / 2 < 0 ||
         Position().y - Height() / 2 >= Gfx::Inst().WindowHeight() ||
         Position().x + Width() / 2 < 0 ||
         Position().x - Width() / 2 >= Gfx::Inst().WindowWidth();
}

namespace {
SpriteId GetBonusSpriteId(Bonus::bonus_t type) noexcept {
  switch (type) {
    case Bonus::extra_speed:
      return SpriteId::BonusSpeed;
    case Bonus::extra_fire:
      return SpriteId::BonusFire;
    case Bonus::extra_shield:
      return SpriteId::BonusShield;
    case Bonus::extra_multi:
      return SpriteId::BonusMulti;
    case Bonus::extra_nuke:
      return SpriteId::BonusNuke;
    default:
      return SpriteId::None;
  }
}
}  // namespace

Bonus::Bonus(Coord pos, bonus_t type)
    : Projectile(PixKeeper::Instance().Get(GetBonusSpriteId(type)), pos,
                 Coord(0, 1)),
      type_(type) {}

Trajectory::Trajectory(const FlightPath* arrival, bool mirrored,
                       const Coord& base_cruise, short grid_col, short grid_row)
    : stage_(arriving),
      arrival_path_(arrival),
      arrival_idx_(0),
      mirrored_(mirrored),
      base_cruise_(base_cruise),
      grid_col_(grid_col),
      grid_row_(grid_row),
      attack_idx_(0) {
  if (!arrival_path_ || arrival_path_->Empty()) {
    throw std::invalid_argument("Empty arrival trajectory supplied");
  }
}

Coord Trajectory::CruiseTarget() const noexcept {
  const int tx = base_cruise_.x + (grid_col_ * g_aliens_hspacing);
  const int ty = BaseCruiseY() + (grid_row_ * g_aliens_vspacing);
  return Coord(static_cast<short>(tx), static_cast<short>(ty));
}

Coord Trajectory::InitPosition() const {
  const auto& pt = (*arrival_path_)[0];
  const float raw_x = mirrored_ ? (1024.0f - pt.x) : pt.x;
  const float raw_y = pt.y;

  const float win_w = static_cast<float>(SdlWindow::Instance().Width());
  const float win_h = static_cast<float>(SdlWindow::Instance().Height());

  const float sx = (raw_x * win_w) / 1024.0f;
  const float sy = (raw_y * win_h) / 1024.0f;

  Coord pos(static_cast<short>(std::round(sx)),
            static_cast<short>(std::round(sy)));
  const short margin_y = static_cast<short>(g_alien_height * 1.15f);
  const short margin_x = static_cast<short>(g_alien_width * 1.15f);

  if (pos.y < 0)
    pos.y = -margin_y;
  else if (pos.y >= SdlWindow::Instance().Height())
    pos.y = static_cast<short>(SdlWindow::Instance().Height() + margin_y);
  if (pos.x < 0)
    pos.x = -margin_x;
  else if (pos.x >= SdlWindow::Instance().Width())
    pos.x = static_cast<short>(SdlWindow::Instance().Width() + margin_x);

  return pos;
}

void Trajectory::NextPositionF(float from_x, float from_y, float velocity,
                               float& out_x, float& out_y) {
  if (velocity <= 0.0f) {
    out_x = from_x;
    out_y = from_y;
    return;
  }

  const float win_w = static_cast<float>(SdlWindow::Instance().Width());
  const float win_h = static_cast<float>(SdlWindow::Instance().Height());

  switch (stage_) {
    case arriving: {
      if (arrival_idx_ >= arrival_path_->Size()) {
        stage_ = joining;
        NextPositionF(from_x, from_y, velocity, out_x, out_y);
        return;
      }

      const auto& raw_pt = (*arrival_path_)[arrival_idx_];
      const float goal_rx = mirrored_ ? (1024.0f - raw_pt.x) : raw_pt.x;
      const float goal_ry = raw_pt.y;

      const float goal_x = (goal_rx * win_w) / 1024.0f;
      const float goal_y = (goal_ry * win_h) / 1024.0f;

      const float dx = goal_x - from_x;
      const float dy = goal_y - from_y;
      const float len = std::sqrt(dx * dx + dy * dy);

      if (len <= velocity) {
        const float remaining = velocity - len;
        ++arrival_idx_;
        if (arrival_idx_ >= arrival_path_->Size()) {
          stage_ = joining;
        }
        if (remaining > 0.001f) {
          NextPositionF(goal_x, goal_y, remaining, out_x, out_y);
        } else {
          out_x = goal_x;
          out_y = goal_y;
        }
        return;
      }

      out_x = from_x + (dx / len) * velocity;
      out_y = from_y + (dy / len) * velocity;
      return;
    }
    case joining: {
      const Coord target = CruiseTarget();
      const float dx = static_cast<float>(target.x) - from_x;
      const float dy = static_cast<float>(target.y) - from_y;
      const float len = std::sqrt(dx * dx + dy * dy);

      const float dock_vel = std::min(velocity, std::max(1.2f, len * 0.12f));

      if (len <= dock_vel) {
        stage_ = cruising;
        out_x = static_cast<float>(target.x);
        out_y = static_cast<float>(target.y);
        return;
      }

      out_x = from_x + (dx / len) * dock_vel;
      out_y = from_y + (dy / len) * dock_vel;
      return;
    }
    case cruising: {
      const Coord target = CruiseTarget();
      out_x = static_cast<float>(target.x);
      out_y = static_cast<float>(target.y);
      return;
    }
    case attacking:
    default: {
      if (attack_idx_ >= attack_.size()) {
        attack_.clear();
        attack_idx_ = 0;
        stage_ = joining;
        out_x = from_x;
        out_y = -static_cast<float>(g_alien_height);
        return;
      }

      const auto& raw_pt = attack_[attack_idx_];
      const float goal_x = (raw_pt.x * win_w) / 1024.0f;
      const float goal_y = (raw_pt.y * win_h) / 1024.0f;

      const float dx = goal_x - from_x;
      const float dy = goal_y - from_y;
      const float len = std::sqrt(dx * dx + dy * dy);
      const float eff_vel = std::max(1.5f, velocity * 0.90f);

      if (len <= eff_vel) {
        const float remaining = eff_vel - len;
        ++attack_idx_;
        if (attack_idx_ >= attack_.size()) {
          attack_.clear();
          attack_idx_ = 0;
          stage_ = joining;
          out_x = from_x;
          out_y = -static_cast<float>(g_alien_height);
          return;
        }
        if (remaining > 0.001f) {
          NextPositionF(goal_x, goal_y, remaining, out_x, out_y);
        } else {
          out_x = goal_x;
          out_y = goal_y;
        }
        return;
      }

      out_x = from_x + (dx / len) * eff_vel;
      out_y = from_y + (dy / len) * eff_vel;
      return;
    }
  }
}

void Trajectory::BuildAttack(Coord from) {
  attack_.clear();
  attack_idx_ = 0;

  std::vector<Vec2f> raw_attack;
  const float min_delta_y = 1024.0f / 35.0f;
  raw_attack.reserve(16);

  const float win_w =
      static_cast<float>(std::max(1, SdlWindow::Instance().Width()));
  const float win_h =
      static_cast<float>(std::max(1, SdlWindow::Instance().Height()));
  Vec2f next((from.x * 1024.0f) / win_w, (from.y * 1024.0f) / win_h);
  raw_attack.push_back(next);

  do {
    float off_x = static_cast<float>(std::rand() % 300 - 150);
    float off_y = static_cast<float>(std::rand() % 160) + min_delta_y;
    next.x += off_x;
    next.y += off_y;
    raw_attack.push_back(next);
  } while (next.y < 1050.0f);

  attack_ = FlightPath::GenerateSmoothSplineF(raw_attack);
  stage_ = attacking;
}

Alien::Alien(const Pix* pix, const Trajectory& trajectory, float speed)
    : Sprite(pix, trajectory.InitPosition()),
      trajectory_(trajectory),
      speed_(speed),
      fx_(static_cast<float>(Position().x)),
      fy_(static_cast<float>(Position().y)),
      dir_x_(0.0f),
      dir_y_(1.0f) {}

void Alien::ForceCruise(Coord target) {
  fx_ = static_cast<float>(target.x);
  fy_ = static_cast<float>(target.y);
  pos_.x = target.x;
  pos_.y = target.y;
  trajectory_.ForceCruise();
}

void Alien::Move() {
  const float s = Gfx::Inst().Scale();
  const float scaled_vel = std::max(1.8f, static_cast<float>(speed_) * s);

  float target_x = fx_, target_y = fy_;
  trajectory_.NextPositionF(fx_, fy_, scaled_vel, target_x, target_y);

  const float dx = target_x - fx_;
  const float dy = target_y - fy_;

  fx_ = target_x;
  fy_ = target_y;

  MoveTo(Coord(static_cast<short>(std::round(fx_)),
               static_cast<short>(std::round(fy_))));

  const float move_dist = std::sqrt(dx * dx + dy * dy);
  if (move_dist > 1e-3f) {
    if (trajectory_.Stage() == Trajectory::cruising) {
      dir_x_ += (0.0f - dir_x_) * 0.14f;
      dir_y_ += (1.0f - dir_y_) * 0.14f;
    } else {
      const float inv_len = 1.0f / move_dist;
      const float target_dir_x = dx * inv_len;
      const float target_dir_y = dy * inv_len;

      dir_x_ += (target_dir_x - dir_x_) * 0.14f;
      dir_y_ += (target_dir_y - dir_y_) * 0.14f;
    }

    const float norm = std::sqrt(dir_x_ * dir_x_ + dir_y_ * dir_y_);
    if (norm > 1e-4f) {
      dir_x_ /= norm;
      dir_y_ /= norm;
    }

    const float target_angle =
        std::atan2(dir_y_, dir_x_) * (180.0f / 3.14159265f) - 90.0f;
    float diff = target_angle - angle_;
    while (diff < -180.0f) diff += 360.0f;
    while (diff > 180.0f) diff -= 360.0f;

    if (std::abs(diff) > 0.25f) {
      constexpr float kMaxTurnRate = 5.5f;
      const float turn = std::clamp(diff * 0.18f, -kMaxTurnRate, kMaxTurnRate);
      angle_ += turn;
    }
  }

  if (spin_timer_ > 0) {
    --spin_timer_;
    const float progress = 1.0f - (static_cast<float>(spin_timer_) /
                                   static_cast<float>(kSpinFrames));
    const float t2 = progress * progress;
    const float smooth =
        t2 * progress * (progress * (progress * 6.0f - 15.0f) + 10.0f);
    spin_angle_ = smooth * 360.0f;
    if (spin_timer_ == 0) {
      spin_angle_ = 0.0f;
    }
  } else if (!has_spun_ && trajectory_.Stage() == Trajectory::attacking) {
    if (std::rand() % 140 == 0) {
      spin_timer_ = kSpinFrames;
      has_spun_ = true;
    }
  }
}

void Alien::Draw(float extra_angle) const {
  if (!pix_) return;

  // Madame Marie Curielien (Alien 4): Rutherford-Bohr Atomic Orbitals & Ionizing Luminescence
  if (pix_ == PixKeeper::Instance().Get(SpriteId::Alien4)) {
    static float s_curie_epoch = 0.0f;
    s_curie_epoch += 0.045f;

    const float aura_rad = Width() * 0.85f;
    const float pulse = 0.82f + 0.18f * std::sin(s_curie_epoch * 3.5f);

    // Core Radium-226 glow & outer Polonium aura
    Gfx::Inst().DrawAura(Position(), aura_rad * pulse, 45, 255, 85, 115);
    Gfx::Inst().DrawAura(Position(), aura_rad * 0.40f, 180, 255, 190, 160);

    SDL_Renderer* rend = Gfx::Inst().GetRenderer();
    const float rx = aura_rad * 0.95f;
    const float ry = aura_rad * 0.32f;

    // Draw the two atomic orbital rings around the vessel
    if (rend) {
      SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(rend, 60, 255, 100, 75);
      constexpr int kOrbitSteps = 32;
      for (int ring = 0; ring < 2; ++ring) {
        const float tilt = (ring == 0) ? 0.628f : -0.628f;
        const float cos_t = std::cos(tilt);
        const float sin_t = std::sin(tilt);
        for (int i = 0; i < kOrbitSteps; ++i) {
          const float a1 = (static_cast<float>(i) / kOrbitSteps) * 6.2831853f;
          const float a2 = (static_cast<float>(i + 1) / kOrbitSteps) * 6.2831853f;
          const float x1 = rx * std::cos(a1), y1 = ry * std::sin(a1);
          const float x2 = rx * std::cos(a2), y2 = ry * std::sin(a2);
          const float rx1 = x1 * cos_t - y1 * sin_t + Position().x;
          const float ry1 = x1 * sin_t + y1 * cos_t + Position().y;
          const float rx2 = x2 * cos_t - y2 * sin_t + Position().x;
          const float ry2 = x2 * sin_t + y2 * cos_t + Position().y;
          SDL_RenderLine(rend, rx1, ry1, rx2, ry2);
        }
      }
    }

    // Dynamic revolving electrons along orbital paths
    const float th1 = s_curie_epoch * 4.2f;
    const float px1 = rx * std::cos(th1);
    const float py1 = ry * std::sin(th1);
    const Coord e1(Position().x + static_cast<short>(px1 * 0.809f - py1 * 0.588f),
                  Position().y + static_cast<short>(px1 * 0.588f + py1 * 0.809f));
    Gfx::Inst().DrawAura(e1, 9.0f * Gfx::Inst().Scale(), 120, 255, 170, 240);

    const float th2 = -s_curie_epoch * 4.8f + 2.0f;
    const float px2 = rx * std::cos(th2);
    const float py2 = ry * std::sin(th2);
    const Coord e2(Position().x + static_cast<short>(px2 * 0.809f + py2 * 0.588f),
                  Position().y + static_cast<short>(-px2 * 0.588f + py2 * 0.809f));
    Gfx::Inst().DrawAura(e2, 9.0f * Gfx::Inst().Scale(), 70, 255, 230, 240);
  }

  // Albert Alienstein (Alien 14): Relativistic Cherenkov Frame-Dragging Aura
  if (pix_ == PixKeeper::Instance().Get(SpriteId::Alien14)) {
    const float aura_rad = Width() * 0.70f;
    Gfx::Inst().DrawAura(Position(), aura_rad, 0, 200, 255, 80);
  }

  pix_->DrawF(Vec2f(fx_, fy_), angle_ + spin_angle_ + extra_angle);
}

void Alien::OnResize(float rx, float ry) noexcept {
  fx_ *= rx;
  fy_ *= ry;
  Sprite::OnResize(rx, ry);
  if (trajectory_.Stage() == Trajectory::cruising) {
    Coord target = trajectory_.CruiseTarget();
    fx_ = static_cast<float>(target.x);
    fy_ = static_cast<float>(target.y);
    MoveTo(target);
  }
}

Coord Alien::CannonPosition() const {
  Coord pos(Position());
  pos.y += Height() - (Height() / 2);
  return pos;
}
