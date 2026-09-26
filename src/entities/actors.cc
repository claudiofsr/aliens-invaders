#include "actors.h"
#include "random_stream.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include "constants.h"
#include "formation_grid.h"
#include "embedded_assets.h"

namespace {
struct OrbitKnot { float c; float s; };
constexpr size_t kOrbitSteps = GameRules::SpecialEntities::kAlien4OrbitKnotSteps;
inline const std::array<OrbitKnot, kOrbitSteps>& GetOrbitKnots() noexcept {
  static const auto tbl = []() {
    std::array<OrbitKnot, kOrbitSteps> knots{};
    for (size_t i = 0; i < kOrbitSteps; ++i) {
      const float a = (static_cast<float>(i) / static_cast<float>(kOrbitSteps)) * 6.28318530718f;
      knots[i] = {std::cos(a), std::sin(a)};
    }
    return knots;
  }();
  return tbl;
}

constexpr auto kSpinAngleTable = []() {
  std::array<float, GameRules::Fleet::kAlienSpinAnimationFrames + 1> tbl{};
  for (std::size_t timer = 0; timer <= static_cast<std::size_t>(GameRules::Fleet::kAlienSpinAnimationFrames); ++timer) {
    if (timer == 0) {
      tbl[timer] = 0.0f;
    } else {
      const float progress = 1.0f - (static_cast<float>(timer) /
                                     static_cast<float>(GameRules::Fleet::kAlienSpinAnimationFrames));
      const float t2 = progress * progress;
      const float smooth = t2 * progress * (progress * (progress * 6.0f - 15.0f) + 10.0f);
      tbl[timer] = smooth * 360.0f;
    }
  }
  return tbl;
}();
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
                       const Coord& base_cruise, int grid_col, int grid_row,
                       std::uint32_t random_seed, int stage_cycle, int used_cols)
    : stage_(arriving),
      arrival_path_(arrival),
      arrival_idx_(0),
      mirrored_(mirrored),
      base_cruise_(base_cruise),
      grid_col_(grid_col),
      grid_row_(grid_row),
      rng_(random_seed),
      attack_idx_(0),
      stage_cycle_(stage_cycle),
      used_cols_(std::max(1, used_cols)) {
  if (!arrival_path_ || arrival_path_->Empty()) {
    throw std::invalid_argument("Empty arrival trajectory supplied");
  }
}

Coord Trajectory::CruiseTarget() const noexcept {
  // Honeycomb station from the constexpr super-table (O(1), symmetric X/Y).
  const auto& slot = GameRules::Fleet::FormationGrid::GetSlot(stage_cycle_, grid_row_, grid_col_);
  return Coord(base_cruise_.x + slot.offset_x, base_cruise_.y + slot.offset_y);
}

Coord Trajectory::InitPosition() const {
  const auto& pt = (*arrival_path_)[0];
  const float raw_x = mirrored_ ? (1024.0f - pt.x) : pt.x;
  const float raw_y = pt.y;

  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());

  const float sx = (raw_x * win_w) / 1024.0f;
  const float sy = (raw_y * win_h) / 1024.0f;

  Coord pos(FastRound(sx), FastRound(sy));
  const int margin_y = static_cast<int>(static_cast<float>(GameRules::Fleet::Height()) * 1.15f);
  const int margin_x = static_cast<int>(static_cast<float>(GameRules::Fleet::Width()) * 1.15f);

  if (pos.y < 0)
    pos.y = -margin_y;
  else if (pos.y >= Gfx::Inst().WindowHeight())
    pos.y = static_cast<int>(Gfx::Inst().WindowHeight() + margin_y);
  if (pos.x < 0)
    pos.x = -margin_x;
  else if (pos.x >= Gfx::Inst().WindowWidth())
    pos.x = static_cast<int>(Gfx::Inst().WindowWidth() + margin_x);

  return pos;
}

void Trajectory::NextPositionF(float from_x, float from_y, float velocity,
                               float& out_x, float& out_y) {
  if (velocity <= 0.0f) {
    out_x = from_x;
    out_y = from_y;
    return;
  }

  // Defer window queries into arriving/attacking branches; cruising/joining run in pure O(1)
  switch (stage_) {
    case arriving: {
      if (arrival_idx_ >= arrival_path_->Size()) {
        stage_ = joining;
        NextPositionF(from_x, from_y, velocity, out_x, out_y);
        return;
      }

      const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
      const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
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

      // Continuous docking: steps smoothly into slot within 1 frame distance, zero teleportation
      if (len <= velocity) {
        stage_ = cruising;
        out_x = static_cast<float>(target.x);
        out_y = static_cast<float>(target.y);
        return;
      }

      const float inv_len = 1.0f / len;
      out_x = from_x + dx * inv_len * velocity;
      out_y = from_y + dy * inv_len * velocity;
      return;
    }
    case cruising: {
      const Coord target = CruiseTarget();
      const float dx = static_cast<float>(target.x) - from_x;
      const float dy = static_cast<float>(target.y) - from_y;
      const float len = std::sqrt(dx * dx + dy * dy);

      if (len <= velocity) {
        out_x = static_cast<float>(target.x);
        out_y = static_cast<float>(target.y);
      } else {
        const float inv_len = 1.0f / len;
        out_x = from_x + dx * inv_len * velocity;
        out_y = from_y + dy * inv_len * velocity;
      }
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

      const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
      const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
      const auto& raw_pt = attack_[attack_idx_];
      const float goal_x = (raw_pt.x * win_w) / 1024.0f;
      const float goal_y = (raw_pt.y * win_h) / 1024.0f;

      const float dx = goal_x - from_x;
      const float dy = goal_y - from_y;
      const float len = std::sqrt(dx * dx + dy * dy);
      const float eff_vel = std::max(GameRules::Fleet::kAttackDiveMinVelocityPixels,
                                     velocity * GameRules::Fleet::kAttackDiveSpeedMultiplier);

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

  const float win_w = static_cast<float>(std::max(1, Gfx::Inst().WindowWidth()));
  const float win_h = static_cast<float>(std::max(1, Gfx::Inst().WindowHeight()));
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

  const float win_w = static_cast<float>(std::max(1, Gfx::Inst().WindowWidth()));
  const float win_h = static_cast<float>(std::max(1, Gfx::Inst().WindowHeight()));

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
  // Alien 4 (Marie Curielien): Sorteia pontos iniciais distintos para cada elétron na órbita
  if (texture_id_ == TextureId::Alien4) {
    electron_offset0_ = trajectory_.UniformFloat(0.0f, 6.2831853f);
    electron_offset1_ = electron_offset0_ + trajectory_.UniformFloat(1.5707963f, 4.7123889f);
    has_electrosphere_ = (trajectory_.UniformFloat(0.0f, 1.0f) <
                        GameRules::SpecialEntities::kAlien4ElectrosphereProbability);
  }
  // Alien 14 (Albert Alienstein): Probability p of acquiring spacetime warp evasion technology
  if (texture_id_ == TextureId::Alien14) {
    has_warp_property_ = (trajectory_.UniformFloat(0.0f, 1.0f) <
                          GameRules::SpecialEntities::kAlien14WarpEvasionProbability);
    warp_evasions_remaining_ = has_warp_property_
                                   ? GameRules::SpecialEntities::kAlien14NumWarpEvasions
                                   : 0;
  }
}

void Alien::RecordWarpGhost() noexcept {
  size_t best_slot = 0;
  float min_alpha = 2.0f;
  for (size_t i = 0; i < warp_ghosts_.size(); ++i) {
    if (!warp_ghosts_[i].active) {
      best_slot = i;
      break;
    }
    if (warp_ghosts_[i].alpha < min_alpha) {
      min_alpha = warp_ghosts_[i].alpha;
      best_slot = i;
    }
  }
  warp_ghosts_[best_slot] = {transform_.position, Angle(), 1.0f, true};
}

void Alien::TriggerAlphaDecay() noexcept {
  if (health_ <= 1 || !has_electrosphere_) return;
  --health_;
  is_alpha_decayed_ = true;
  object_.scale = GameRules::SpecialEntities::kAlien4AlphaDecayScale;
  UpdateAABB();
}

void Alien::ExecuteWarp(const GameRules::Fleet::FormationGrid::WarpDecision& decision) noexcept {
  --warp_evasions_remaining_;
  is_warping_ = true;
  warp_frames_left_ = GameRules::SpecialEntities::kAlien14WarpMaxFrames;
  warp_cooldown_timer_ = FastRound(
      GameRules::SpecialEntities::kAlien14WarpCooldownSeconds *
      GameRules::Simulation::kSimulationFrequencyHz);

  // Priority 5: Relocate station permanently and abort any dive back to cruising formation
  trajectory_.RelocateStation(decision.col, decision.row);
  trajectory_.ForceCruise();
  attack_wait_timer_ = trajectory_.UniformInt(
      180, std::max(180, trajectory_.GetMaxAttackWaitFrames()));
  has_reached_formation_ = true;

  // Priority 7: Single cached destination (consumed strictly by Move)
  warp_dest_ = decision.target_pos;

  Gfx::Inst().AddFloatingText(Position(), "RELATIVISTIC WARP EVASION!", 0, 240, 255,
                             GameRules::Visuals::kFloatingTextNoticeFontSize, 65);
  Gfx::Inst().TriggerFlash(0, 200, 255, 6);
}

void Alien::TriggerSpacetimeWarp(
    const std::array<std::uint16_t, GameRules::Fleet::FormationGrid::kMaxGridRows>& occupancy,
    int player_x, int player_y) noexcept {
  // Evasion activates at any point in trajectory while evasions remain, off cooldown and not mid-warp
  if (warp_evasions_remaining_ <= 0 || is_warping_ || warp_cooldown_timer_ > 0) {
    return;
  }

  using FG = GameRules::Fleet::FormationGrid;
  const int origin_col = trajectory_.GridCol();
  const int origin_row = trajectory_.GridRow();
  const int used_cols = std::clamp(trajectory_.UsedCols(), 1, FG::kMaxGridCols);

  const float s = Gfx::Inst().Scale();
  const int half_h = GameRules::Fleet::Height() / 2;
  const int max_y = Gfx::Inst().WindowHeight() - half_h - FastRound(
      static_cast<float>(GameRules::Fleet::kFleetMarginYMaxOffsetPixels) * s);
  const int sp_y = std::max(1, FG::SpacingY());
  const int max_margin_row = std::clamp((max_y - FG::BaseCruiseY()) / sp_y, 0, FG::kMaxGridRows - 1);
  const int used_rows = max_margin_row + 1;

  const auto decision = FG::ComputeRelativisticWarp(
      origin_col, origin_row, used_cols, used_rows,
      player_x, player_y, trajectory_.BaseCruise(), trajectory_.StageCycle(),
      s, Gfx::Inst().WindowWidth(), Gfx::Inst().WindowHeight(),
      occupancy, trajectory_.GetRng());

  ExecuteWarp(decision);
}

void Alien::ForceCruise(Coord target) {
  transform_.position = target.ToVec2f();
  transform_.previous_position = transform_.position;
  MoveTo(target);
  trajectory_.ForceCruise();
  has_reached_formation_ = true;
  UpdateAABB();
}

void Alien::Move() {
  transform_.previous_position = transform_.position;

  const float s = Gfx::Inst().Scale();
  const float move_speed_mult = is_alpha_decayed_
                                    ? GameRules::SpecialEntities::kAlien4DecayedSpeedMult
                                    : 1.0f;
  const float scaled_vel = std::max(GameRules::Fleet::kFleetMinVelocityPixels,
                                    static_cast<float>(speed_) * s * move_speed_mult);

  // Decrement warp cooldown timer
  if (warp_cooldown_timer_ > 0) {
    --warp_cooldown_timer_;
  }

  // Update fading relativistic afterimage ghost trail
  for (auto& g : warp_ghosts_) {
    if (g.active) {
      g.alpha -= GameRules::SpecialEntities::kAlien14WarpGhostFadeSpeed;
      if (g.alpha <= 0.01f) {
        g.active = false;
      }
    }
  }

  float target_x = transform_.position.x, target_y = transform_.position.y;
  if (is_warping_) {
    RecordWarpGhost();
    const float warp_step = GameRules::SpecialEntities::kAlien14WarpSpeedPixels * s;
    // Priority 7: Consume strictly the cached destination; never re-sample per frame (prevents jitter)
    const Coord dest = warp_dest_;
    const float diff_x = static_cast<float>(dest.x) - transform_.position.x;
    const float diff_y = static_cast<float>(dest.y) - transform_.position.y;
    const float diff_dist = std::sqrt(diff_x * diff_x + diff_y * diff_y);

    bool arrived = (diff_dist <= warp_step);
    if (warp_frames_left_ > 0) {
      --warp_frames_left_;
    }
    if (arrived || warp_frames_left_ <= 0) {
      // Snap to cached destination and always clear the warping flag
      transform_.position.x = static_cast<float>(dest.x);
      transform_.position.y = static_cast<float>(dest.y);
      is_warping_ = false;
      warp_frames_left_ = 0;
    } else {
      const float inv_dist = 1.0f / diff_dist;
      transform_.position.x += diff_x * inv_dist * warp_step;
      transform_.position.y += diff_y * inv_dist * warp_step;
    }
    target_x = transform_.position.x;
    target_y = transform_.position.y;
  } else {
    const Trajectory::stage_t prev_stage = trajectory_.Stage();
    trajectory_.NextPositionF(transform_.position.x, transform_.position.y, scaled_vel, target_x, target_y);
    if (trajectory_.Stage() == Trajectory::cruising) {
      has_reached_formation_ = true;
      if (prev_stage != Trajectory::cruising && attack_wait_timer_ <= 0) {
        attack_wait_timer_ = trajectory_.UniformInt(30, trajectory_.GetMaxAttackWaitFrames());
      }
    }
  }

  const float delta_jump_x = std::abs(target_x - transform_.previous_position.x);
  const float delta_jump_y = std::abs(target_y - transform_.previous_position.y);
  if (!is_warping_ && (delta_jump_x > 180.0f || delta_jump_y > 180.0f)) {
    transform_.previous_position = Vec2f(target_x, target_y);
  }

  transform_.position = Vec2f(target_x, target_y);
  MoveTo(Coord(transform_.position));

  const float dx = target_x - transform_.previous_position.x;
  const float dy = target_y - transform_.previous_position.y;
  const float move_dist = std::sqrt(dx * dx + dy * dy);

  if (move_dist > 1e-3f) {
    if (trajectory_.Stage() == Trajectory::cruising) {
      dir_x_ += (0.0f - dir_x_) * GameRules::Fleet::kTurnDampingFactor;
      dir_y_ += (1.0f - dir_y_) * GameRules::Fleet::kTurnDampingFactor;
    } else {
      const float inv_len = 1.0f / move_dist;
      const float target_dir_x = dx * inv_len;
      const float target_dir_y = dy * inv_len;

      dir_x_ += (target_dir_x - dir_x_) * GameRules::Fleet::kTurnDampingFactor;
      dir_y_ += (target_dir_y - dir_y_) * GameRules::Fleet::kTurnDampingFactor;
    }

    const float norm = std::sqrt(dir_x_ * dir_x_ + dir_y_ * dir_y_);
    if (norm > 1e-4f) {
      dir_x_ /= norm;
      dir_y_ /= norm;
    }

    // Fast path: avoid expensive atan2 during horizontal cruise when alien is already upright
    if (trajectory_.Stage() == Trajectory::cruising && std::abs(angle_) < 0.05f && std::abs(dir_x_) < 0.01f) {
      angle_ = 0.0f;
    } else {
    // Fast-path: skip expensive atan2f when alien is upright in horizontal cruising formation
    if (trajectory_.Stage() == Trajectory::cruising && std::abs(angle_) < 0.05f && std::abs(dir_x_) < 0.01f) {
      angle_ = 0.0f;
    } else {
      const float target_angle =
          std::atan2(dir_y_, dir_x_) * (180.0f / 3.14159265f) - 90.0f;
      float diff = target_angle - angle_;
      while (diff < -180.0f) diff += 360.0f;
      while (diff > 180.0f) diff -= 360.0f;

      if (std::abs(diff) > 0.25f) {
        const float turn = std::clamp(diff * 0.18f, -GameRules::Fleet::kMaxTurnRateDeg, GameRules::Fleet::kMaxTurnRateDeg);
        angle_ += turn;
      }
    }
    }
  }

  if (spin_timer_ > 0) {
    --spin_timer_;
    spin_angle_ = kSpinAngleTable[static_cast<std::size_t>(std::max(0, spin_timer_))];
  } else if (!has_spun_ && trajectory_.Stage() == Trajectory::attacking) {
    if (trajectory_.Chance(1, 140)) {
      spin_timer_ = GameRules::Fleet::kAlienSpinAnimationFrames;
      has_spun_ = true;
    }
  }

  // Evolução determinística de animações no passo fixo de 60 Hz (independente do monitor)
  if (is_kamikaze_) {
    kamikaze_phase_ += GameRules::Visuals::kKamikazePulsePhaseStep;
  }
  if (texture_id_ == TextureId::Alien4 && has_electrosphere_) {
    const float speed_mult = is_alpha_decayed_
                                 ? GameRules::SpecialEntities::kAlien4DecayedOrbitSpeedMult
                                 : 1.0f;
    electron_phase_ += GameRules::SpecialEntities::kAlien4ElectronPhaseStep * speed_mult;
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

  const float w = static_cast<float>(Width());
  if (is_kamikaze_) {
    const float pulse = GameRules::Visuals::kKamikazePulseMid +
                        GameRules::Visuals::kKamikazePulseAmp *
                            std::sin(kamikaze_phase_ * GameRules::Visuals::kKamikazePulseFrequency);
    const float aura_rad = w * (GameRules::Visuals::kKamikazeAuraRadiusScale * pulse + GameRules::Visuals::kKamikazeAuraRadiusBias);

    Gfx::Inst().DrawAura(render_pos, aura_rad * GameRules::Visuals::kKamikazeAuraOuterScale, 255, 30, 40, 150);
    Gfx::Inst().DrawAura(render_pos, aura_rad * GameRules::Visuals::kKamikazeAuraMidScale, 255, 140, 0, 190);
    Gfx::Inst().DrawAura(render_pos, aura_rad * GameRules::Visuals::kKamikazeAuraInnerScale, 255, 240, 220, 230);
  }

  if (texture_id_ == TextureId::Alien4) {
    if (has_electrosphere_) {
      const float aura_rad = w * GameRules::SpecialEntities::kAlien4AuraRadiusScale;
      const float pulse = GameRules::SpecialEntities::kAlien4AuraPulseMid + GameRules::SpecialEntities::kAlien4AuraPulseAmplitude * std::sin(electron_phase_ * GameRules::SpecialEntities::kAlien4AuraPulseFrequency);

      Gfx::Inst().DrawAura(render_pos, aura_rad * pulse, 45, 255, 85, 115);
      Gfx::Inst().DrawAura(render_pos, aura_rad * GameRules::SpecialEntities::kAlien4InnerAuraScale, 180, 255, 190, 160);

      SDL_Renderer* rend = Gfx::Inst().GetRenderer();
      const float rx = aura_rad * GameRules::SpecialEntities::kAlien4ElectronRadiusScale;
      const float ry = aura_rad * GameRules::SpecialEntities::kAlien4ElectronMinorRadiusScale;

      if (rend) {
        SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(rend, 60, 255, 100, 85);
        // CPU cache: orbit tilt is constant; sin/cos computed once per process.
        static const float kTiltCos = std::cos(GameRules::SpecialEntities::kAlien4OrbitTiltRad);
        static const float kTiltSin = std::sin(GameRules::SpecialEntities::kAlien4OrbitTiltRad);
        const float ring_cos[2] = {kTiltCos, kTiltCos};
        const float ring_sin[2] = {kTiltSin, -kTiltSin};
        const auto& knots = GetOrbitKnots();

        SDL_FPoint ring_pts[37];
        for (int ring = 0; ring < 2; ++ring) {
          const float cos_t = ring_cos[ring];
          const float sin_t = ring_sin[ring];
          for (size_t i = 0; i < 36; ++i) {
            const float x = rx * knots[i].c;
            const float y = ry * knots[i].s;
            ring_pts[i].x = x * cos_t - y * sin_t + static_cast<float>(render_pos.x);
            ring_pts[i].y = x * sin_t + y * cos_t + static_cast<float>(render_pos.y);
          }
          ring_pts[36] = ring_pts[0]; // Fecha o anel perfeitamente
          SDL_RenderLines(rend, ring_pts, 37);
        }

        const float speed = GameRules::SpecialEntities::kAlien4ElectronAngularSpeed;
        // Rotação em sentidos opostos com posições iniciais assíncronas (sem espelhamento estático)
        const float theta0 = electron_phase_ * speed + electron_offset0_;
        const float theta1 = -electron_phase_ * speed + electron_offset1_;

        const float cos_t0 = kTiltCos;
        const float sin_t0 = kTiltSin;
        const float e0_ox = rx * std::cos(theta0);
        const float e0_oy = ry * std::sin(theta0);
        const float e0_x = e0_ox * cos_t0 - e0_oy * sin_t0 + static_cast<float>(render_pos.x);
        const float e0_y = e0_ox * sin_t0 + e0_oy * cos_t0 + static_cast<float>(render_pos.y);

        const float cos_t1 = kTiltCos;
        const float sin_t1 = -kTiltSin;
        const float e1_ox = rx * std::cos(theta1);
        const float e1_oy = ry * std::sin(theta1);
        const float e1_x = e1_ox * cos_t1 - e1_oy * sin_t1 + static_cast<float>(render_pos.x);
        const float e1_y = e1_ox * sin_t1 + e1_oy * cos_t1 + static_cast<float>(render_pos.y);

        const float s = Gfx::Inst().Scale();
        const float electron_aura_radius = GameRules::SpecialEntities::kAlien4ElectronAuraRadiusPixels * s;
        const float electron_core_size = GameRules::SpecialEntities::kAlien4ElectronRadiusPixels * s;
        const Coord e0_coord(FastRound(e0_x), FastRound(e0_y));
        const Coord e1_coord(FastRound(e1_x), FastRound(e1_y));

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
    const float aura_rad = w * GameRules::Visuals::kAlien14AuraRadiusScale;
    Gfx::Inst().DrawAura(render_pos, aura_rad, 0, 200, 255, 80);

    // Render fading relativistic afterimage ghost trail
    for (const auto& ghost : warp_ghosts_) {
      if (ghost.active && ghost.alpha > 0.01f) {
        const float ghost_a = std::clamp(ghost.alpha, 0.0f, 1.0f);
        const Coord g_pos(ghost.pos);
        Gfx::Inst().DrawAura(g_pos, aura_rad * (1.1f + 0.3f * (1.0f - ghost_a)), 0, 230, 255, static_cast<uint8_t>(ghost_a * 150.0f));
        Gfx::Inst().DrawAura(g_pos, aura_rad * 0.4f, 180, 255, 255, static_cast<uint8_t>(ghost_a * 200.0f));
        pix->DrawF(ghost.pos, ghost.angle, 0, 220, 255, static_cast<uint8_t>(ghost_a * 220.0f));
      }
    }

    if (is_warping_) {
      Gfx::Inst().DrawAura(render_pos, aura_rad * 1.5f, 0, 255, 255, 180);
      Gfx::Inst().DrawAura(render_pos, aura_rad * 0.7f, 255, 255, 255, 220);
    }
  }

  if (texture_id_ == TextureId::Alien4 && is_alpha_decayed_) {
    Gfx::Inst().DrawAura(render_pos, w * 1.1f, 50, 255, 120, 150);
  }

  if (object_.scale != 1.0f) {
    pix->DrawSized(render_pos, Width(), Height(), angle_ + spin_angle_ + extra_angle);
  } else {
    pix->DrawF(interp, angle_ + spin_angle_ + extra_angle);
  }
}

void Alien::OnResize(float rx, float ry) noexcept {
  transform_.position.x *= rx;
  transform_.position.y *= ry;
  transform_.previous_position.x *= rx;
  transform_.previous_position.y *= ry;
  for (auto& g : warp_ghosts_) {
    if (g.active) {
      g.pos.x *= rx;
      g.pos.y *= ry;
    }
  }
  Coord p = object_.transform.position;
  p.x = FastRound(static_cast<double>(p.x) * static_cast<double>(rx));
  p.y = FastRound(static_cast<double>(p.y) * static_cast<double>(ry));
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
