#include "actors.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include "constants.h"
#include "embedded_assets.h"
#include "sdl_window.h"

namespace {
TextureId GetBonusTextureId(Bonus::bonus_t type) noexcept {
  switch (type) {
    case Bonus::extra_speed:  return TextureId::BonusSpeed;
    case Bonus::extra_fire:   return TextureId::BonusFire;
    case Bonus::extra_shield: return TextureId::BonusShield;
    case Bonus::extra_multi:  return TextureId::BonusMulti;
    case Bonus::extra_nuke:   return TextureId::BonusNuke;
    default:                  return TextureId::None;
  }
}

struct OrbitKnot { float c; float s; };
inline const std::array<OrbitKnot, 36>& GetOrbitKnots() noexcept {
  static const auto tbl = []() {
    std::array<OrbitKnot, 36> knots{};
    for (size_t i = 0; i < 36; ++i) {
      const float a = (static_cast<float>(i) / 36.0f) * 6.2831853f;
      knots[i] = {std::cos(a), std::sin(a)};
    }
    return knots;
  }();
  return tbl;
}
}  // namespace

Bonus::Bonus(Coord pos, bonus_t type) : type_(type) {
  const auto* pix = PixKeeper::Instance().Get(GetBonusTextureId(type));
  object_.renderable.pix = pix;
  object_.transform.position = pos;
  object_.motion.velocity = Coord(0, 1);
  if (pix) {
    object_.collider.aabb = AABB(static_cast<float>(pix->Width()) * 0.5f,
                                 static_cast<float>(pix->Height()) * 0.5f);
  }
}

bool Bonus::Out() const noexcept {
  return Position().y - Height() / 2 >= Gfx::Inst().WindowHeight();
}

Trajectory::Trajectory(const FlightPath* arrival, bool mirrored,
                       const Coord& base_cruise, int grid_col, int grid_row, std::uint32_t random_seed)
    : stage_(arriving),
      arrival_path_(arrival),
      arrival_idx_(0),
      mirrored_(mirrored),
      base_cruise_(base_cruise),
      grid_col_(grid_col),
      grid_row_(grid_row),
      rng_(random_seed),
      attack_idx_(0) {
  if (!arrival_path_ || arrival_path_->Empty()) {
    throw std::invalid_argument("Empty arrival trajectory supplied");
  }
}

Coord Trajectory::CruiseTarget() const noexcept {
  const int tx = base_cruise_.x + (grid_col_ * GameRules::Fleet::HSpacing());
  const int ty = GameRules::Fleet::BaseCruiseY() + (grid_row_ * GameRules::Fleet::VSpacing());
  return Coord(tx, ty);
}

Coord Trajectory::InitPosition() const {
  const auto& pt = (*arrival_path_)[0];
  const float raw_x = mirrored_ ? (1024.0f - pt.x) : pt.x;
  const float raw_y = pt.y;

  const float win_w = static_cast<float>(SdlWindow::Instance().Width());
  const float win_h = static_cast<float>(SdlWindow::Instance().Height());

  const float sx = (raw_x * win_w) / 1024.0f;
  const float sy = (raw_y * win_h) / 1024.0f;

  Coord pos(static_cast<int>(std::round(sx)), static_cast<int>(std::round(sy)));
  const int margin_y = static_cast<int>(static_cast<float>(GameRules::Fleet::Height()) * 1.15f);
  const int margin_x = static_cast<int>(static_cast<float>(GameRules::Fleet::Width()) * 1.15f);

  if (pos.y < 0)
    pos.y = -margin_y;
  else if (pos.y >= SdlWindow::Instance().Height())
    pos.y = static_cast<int>(SdlWindow::Instance().Height() + margin_y);
  if (pos.x < 0)
    pos.x = -margin_x;
  else if (pos.x >= SdlWindow::Instance().Width())
    pos.x = static_cast<int>(SdlWindow::Instance().Width() + margin_x);

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
        out_y = -static_cast<float>(GameRules::Fleet::Height());
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
          out_y = -static_cast<float>(GameRules::Fleet::Height());
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

  const float win_w = static_cast<float>(std::max(1, SdlWindow::Instance().Width()));
  const float win_h = static_cast<float>(std::max(1, SdlWindow::Instance().Height()));
  Vec2f next((static_cast<float>(from.x) * 1024.0f) / win_w, (static_cast<float>(from.y) * 1024.0f) / win_h);
  raw_attack.push_back(next);

  do {
    float off_x = static_cast<float>(rng_.UniformInt(-150, 149));
    float off_y = static_cast<float>(rng_.UniformInt(0, 159)) + min_delta_y;
    next.x += off_x;
    next.y += off_y;
    raw_attack.push_back(next);
  } while (next.y < 1050.0f);

  attack_ = FlightPath::GenerateSmoothSplineF(raw_attack);
  stage_ = attacking;
}

void Trajectory::BuildKamikazeDive(Coord from, Coord target_player) {
  attack_.clear();
  attack_idx_ = 0;

  const float win_w = static_cast<float>(std::max(1, SdlWindow::Instance().Width()));
  const float win_h = static_cast<float>(std::max(1, SdlWindow::Instance().Height()));

  const Vec2f start((static_cast<float>(from.x) * 1024.0f) / win_w, (static_cast<float>(from.y) * 1024.0f) / win_h);
  const Vec2f target((static_cast<float>(target_player.x) * 1024.0f) / win_w, (static_cast<float>(target_player.y) * 1024.0f) / win_h);

  const float mid_x = start.x + (target.x - start.x) * 0.45f + static_cast<float>(rng_.UniformInt(-50, 49));
  const float mid_y = start.y + (target.y - start.y) * 0.45f;
  const Vec2f waypoint(mid_x, mid_y);
  const Vec2f overshoot(target.x, target.y + 140.0f);

  std::vector<Vec2f> raw_pts = {start, waypoint, target, overshoot};
  attack_ = FlightPath::GenerateSmoothSplineF(raw_pts);
  stage_ = attacking;
}

Alien::Alien(const Pix* pix, const Trajectory& trajectory, float speed, TextureId texture_id)
    : trajectory_(trajectory),
      speed_(speed),
      transform_(trajectory.InitPosition().ToVec2f()),
      texture_id_(texture_id),
      dir_x_(0.0f),
      dir_y_(1.0f) {
  if (!pix) throw std::invalid_argument("Null pixmap for alien");
  object_.renderable.pix = pix;
  object_.transform.position = trajectory.InitPosition();
  UpdateAABB();
}

void Alien::ForceCruise(Coord target) {
  transform_.position = target.ToVec2f();
  transform_.previous_position = transform_.position;
  MoveTo(target);
  trajectory_.ForceCruise();
  UpdateAABB();
}

void Alien::Move() {
  transform_.previous_position = transform_.position;

  const float s = Gfx::Inst().Scale();
  const float scaled_vel = std::max(1.8f, static_cast<float>(speed_) * s);

  float target_x = transform_.position.x, target_y = transform_.position.y;
  trajectory_.NextPositionF(transform_.position.x, transform_.position.y, scaled_vel, target_x, target_y);

  const float delta_jump = std::abs(target_y - transform_.position.y);
  if (delta_jump > 180.0f) {
    transform_.previous_position = Vec2f(target_x, target_y);
  }

  transform_.position = Vec2f(target_x, target_y);
  MoveTo(Coord(transform_.position));

  const float dx = target_x - transform_.previous_position.x;
  const float dy = target_y - transform_.previous_position.y;
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
                                   static_cast<float>(GameRules::Fleet::kAlienSpinAnimationFrames));
    const float t2 = progress * progress;
    const float smooth =
        t2 * progress * (progress * (progress * 6.0f - 15.0f) + 10.0f);
    spin_angle_ = smooth * 360.0f;
    if (spin_timer_ == 0) {
      spin_angle_ = 0.0f;
    }
  } else if (!has_spun_ && trajectory_.Stage() == Trajectory::attacking) {
    if (trajectory_.Chance(1, 140)) {
      spin_timer_ = GameRules::Fleet::kAlienSpinAnimationFrames;
      has_spun_ = true;
    }
  }
}

void Alien::Draw(float extra_angle) const {
  DrawInterpolated(1.0f, extra_angle);
}

void Alien::DrawInterpolated(float alpha, float extra_angle) const {
  const Pix* pix = object_.renderable.pix;
  if (!pix) return;

  const Vec2f interp = transform_.InterpolatedPosition(alpha);
  const Coord render_pos(interp);

  if (is_kamikaze_) {
    kamikaze_phase_ += 0.08f;
    const float pulse = 0.75f + 0.25f * std::sin(kamikaze_phase_ * 4.5f);
    const float aura_rad = static_cast<float>(Width()) * (0.88f * pulse + 0.12f);

    Gfx::Inst().DrawAura(render_pos, aura_rad * 1.30f, 255, 30, 40, 150);
    Gfx::Inst().DrawAura(render_pos, aura_rad * 0.75f, 255, 140, 0, 190);
    Gfx::Inst().DrawAura(render_pos, aura_rad * 0.35f, 255, 240, 220, 230);
  }

  if (texture_id_ == TextureId::Alien4) {
    if (has_electrosphere_) {
      static float s_curie_epoch = 0.0f;
      s_curie_epoch += 0.020f;

      const float aura_rad = static_cast<float>(Width()) * 0.85f;
      const float pulse = 0.82f + 0.18f * std::sin(s_curie_epoch * 3.5f);

      Gfx::Inst().DrawAura(render_pos, aura_rad * pulse, 45, 255, 85, 115);
      Gfx::Inst().DrawAura(render_pos, aura_rad * 0.40f, 180, 255, 190, 160);

      SDL_Renderer* rend = Gfx::Inst().GetRenderer();
      const float rx = aura_rad * GameRules::SpecialEntities::kAlien4ElectronRadiusScale;
      const float ry = aura_rad * GameRules::SpecialEntities::kAlien4ElectronMinorRadiusScale;

      if (rend) {
        SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(rend, 60, 255, 100, 85);
        const float tilt0 = GameRules::SpecialEntities::kAlien4OrbitTiltRad;
        const float tilts[2] = {tilt0, -tilt0};
        const auto& knots = GetOrbitKnots();

        for (int ring = 0; ring < 2; ++ring) {
          const float tilt = tilts[ring];
          const float cos_t = std::cos(tilt);
          const float sin_t = std::sin(tilt);
          for (size_t i = 0; i < 36; ++i) {
            const size_t next_i = (i + 1 == 36) ? 0 : (i + 1);
            const float x1 = rx * knots[i].c, y1 = ry * knots[i].s;
            const float x2 = rx * knots[next_i].c, y2 = ry * knots[next_i].s;
            const float rx1 = x1 * cos_t - y1 * sin_t + static_cast<float>(render_pos.x);
            const float ry1 = x1 * sin_t + y1 * cos_t + static_cast<float>(render_pos.y);
            const float rx2 = x2 * cos_t - y2 * sin_t + static_cast<float>(render_pos.x);
            const float ry2 = x2 * sin_t + y2 * cos_t + static_cast<float>(render_pos.y);
            SDL_RenderLine(rend, rx1, ry1, rx2, ry2);
          }
        }

        const float speed = GameRules::SpecialEntities::kAlien4ElectronAngularSpeed;
        const float theta0 = s_curie_epoch * speed;
        const float theta1 = -s_curie_epoch * speed + 3.14159265f;

        const float cos_t0 = std::cos(tilts[0]);
        const float sin_t0 = std::sin(tilts[0]);
        const float e0_ox = rx * std::cos(theta0);
        const float e0_oy = ry * std::sin(theta0);
        const float e0_x = e0_ox * cos_t0 - e0_oy * sin_t0 + static_cast<float>(render_pos.x);
        const float e0_y = e0_ox * sin_t0 + e0_oy * cos_t0 + static_cast<float>(render_pos.y);

        const float cos_t1 = std::cos(tilts[1]);
        const float sin_t1 = std::sin(tilts[1]);
        const float e1_ox = rx * std::cos(theta1);
        const float e1_oy = ry * std::sin(theta1);
        const float e1_x = e1_ox * cos_t1 - e1_oy * sin_t1 + static_cast<float>(render_pos.x);
        const float e1_y = e1_ox * sin_t1 + e1_oy * cos_t1 + static_cast<float>(render_pos.y);

        const float s = Gfx::Inst().Scale();
        const float electron_aura_radius = GameRules::SpecialEntities::kAlien4ElectronAuraRadiusPixels * s;
        const float electron_core_size = GameRules::SpecialEntities::kAlien4ElectronRadiusPixels * s;
        const Coord e0_coord(static_cast<int>(std::round(e0_x)), static_cast<int>(std::round(e0_y)));
        const Coord e1_coord(static_cast<int>(std::round(e1_x)), static_cast<int>(std::round(e1_y)));

        Gfx::Inst().DrawAura(e0_coord, electron_aura_radius, 60, 255, 100, 240);
        Gfx::Inst().DrawAura(e1_coord, electron_aura_radius, 0, 230, 255, 240);
        Gfx::Inst().DrawAura(e0_coord, electron_aura_radius * 0.5f, 220, 255, 200, 255);
        Gfx::Inst().DrawAura(e1_coord, electron_aura_radius * 0.5f, 200, 255, 255, 255);

        SDL_SetRenderDrawColor(rend, 255, 255, 255, 255);
        const SDL_FRect r0 = {e0_x - electron_core_size * 0.5f, e0_y - electron_core_size * 0.5f,
                              electron_core_size, electron_core_size};
        const SDL_FRect r1 = {e1_x - electron_core_size * 0.5f, e1_y - electron_core_size * 0.5f,
                              electron_core_size, electron_core_size};
        SDL_RenderFillRect(rend, &r0);
        SDL_RenderFillRect(rend, &r1);
      }
    }
  }

  if (texture_id_ == TextureId::Alien14) {
    const float aura_rad = static_cast<float>(Width()) * 0.70f;
    Gfx::Inst().DrawAura(render_pos, aura_rad, 0, 200, 255, 80);
  }

  pix->DrawF(interp, angle_ + spin_angle_ + extra_angle);
}

void Alien::OnResize(float rx, float ry) noexcept {
  transform_.position.x *= rx;
  transform_.position.y *= ry;
  transform_.previous_position.x *= rx;
  transform_.previous_position.y *= ry;
  Coord p = object_.transform.position;
  p.x = static_cast<int32_t>(std::lround(static_cast<double>(p.x) * static_cast<double>(rx)));
  p.y = static_cast<int32_t>(std::lround(static_cast<double>(p.y) * static_cast<double>(ry)));
  object_.transform.position = p;
  UpdateAABB();
  if (trajectory_.Stage() == Trajectory::cruising) {
    Coord target = trajectory_.CruiseTarget();
    transform_.position = target.ToVec2f();
    transform_.previous_position = transform_.position;
    MoveTo(target);
  }
}

Coord Alien::CannonPosition() const {
  Coord pos(Position());
  pos.y += Height() - (Height() / 2);
  return pos;
}
