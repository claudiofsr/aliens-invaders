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
#include "math_types.h"

namespace {
struct OrbitKnot { float c; float s; };
constexpr size_t kOrbitSteps = GameRules::SpecialEntities::kAlien4OrbitKnotSteps;
inline const std::array<OrbitKnot, kOrbitSteps>& GetOrbitKnots() noexcept {
  static const auto tbl = []() {
    std::array<OrbitKnot, kOrbitSteps> knots{};
    for (size_t i = 0; i < kOrbitSteps; ++i) {
      const float a = (static_cast<float>(i) / static_cast<float>(kOrbitSteps)) * (2.0f * Pi);
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

Alien::Alien(const Pix* pix, const Trajectory& trajectory, float speed, TextureId texture_id)
    : trajectory_(trajectory),
      speed_(speed),
      transform_(trajectory.InitPosition().ToVec2f()),
      texture_id_(texture_id) {
  // Construction is just the FIRST spawn of a fresh heap block: the body is the very same path
  // Respawn() runs when re-arming a pooled alien (Deep DRY, AGENT 5), so the two lives can ...
  FinishSpawn(pix);
}

void Alien::Respawn(const Pix* pix, const Trajectory& trajectory, float speed,
                    TextureId texture_id) {
  // Re-arm the pooled alien in place. Reusing trajectory capacity avoids
  // fresh allocation during respawn.
  trajectory_ = trajectory;
  speed_ = speed;
  texture_id_ = texture_id;
  transform_ = Transform2D(trajectory.InitPosition().ToVec2f());
  FinishSpawn(pix);
}

void Alien::ResetCombatState() noexcept {
  angle_ = 0.0f;
  spin_angle_ = 0.0f;
  prev_angle_total_ = 0.0f;
  spin_timer_ = 0;
  has_spun_ = false;
  attack_wait_timer_ = 0;
  is_kamikaze_ = false;
  is_wanderer_ = false;
  has_electrosphere_ = false;
  health_ = GameRules::SpecialEntities::kAlien4InitialHealth;
  is_alpha_decayed_ = false;
  kamikaze_phase_ = 0.0f;
  electron_phase_ = 0.0f;
  electron_offset0_ = 0.0f;
  electron_offset1_ = 0.0f;
  has_warp_property_ = false;
  warp_evasions_remaining_ = 0;
  warp_cooldown_timer_ = 0;
  is_warping_ = false;
  warp_frames_left_ = 0;
  warp_dest_ = Coord(0, 0);
  warp_ghosts_.fill(WarpGhost{});
}

void Alien::FinishSpawn(const Pix* pix) {
  if (!pix) throw std::invalid_argument("Null pixmap for alien");
  ResetCombatState();
  // Reset all mutable state so a recycled object matches a fresh spawn.
  object_ = simulation::GameObject{};
  object_.renderable.pix = pix;
  object_.transform.position = trajectory_.InitPosition();
  UpdateAABB();
  // Alien 4 (Marie Curielien): draw distinct orbital starting phases for each electron
  if (texture_id_ == TextureId::Alien4) {
    electron_offset0_ = trajectory_.UniformFloat(0.0f, 2.0f * Pi);
    electron_offset1_ = electron_offset0_ + trajectory_.UniformFloat(Pi * 0.5f, Pi * 1.5f);
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

  // Priority 4c: the drawn cell becomes the permanent honeycomb station and
  // any dive aborts back to cruising formation
  trajectory_.RelocateStation(decision.col, decision.row);
  trajectory_.ForceCruise();
  attack_wait_timer_ = trajectory_.UniformInt(
      GameRules::SpecialEntities::kAlien14PostWarpAttackWaitMinFrames,
      std::max(GameRules::SpecialEntities::kAlien14PostWarpAttackWaitMinFrames,
               trajectory_.GetMaxAttackWaitFrames()));

  // Priority 4b: single cached destination (consumed strictly by Move)
  warp_dest_ = decision.target_pos;

  Gfx::Inst().AddFloatingText(Position(), "RELATIVISTIC WARP EVASION!", 0, 240, 255,
                             GameRules::Visuals::kFloatingTextNoticeFontSize, 65);
  Gfx::Inst().TriggerFlash(0, 200, 255, 6);
}

void Alien::TriggerSpacetimeWarp(
    const std::array<std::uint16_t, GameRules::Fleet::FormationGrid::kMaxGridRows>& occupancy,
    int player_x, int player_y, int formation_rows) noexcept {
  // Spec activation rule: the only hard gate is the jump budget (at least one evasion left). A
  // mid-transit restart cannot happen -- while warping, the physical colliders are deactivated, ...
  if (warp_evasions_remaining_ <= 0) {
    return;
  }

  using FG = GameRules::Fleet::FormationGrid;
  const int origin_col = trajectory_.GridCol();
  const int origin_row = trajectory_.GridRow();
  const int used_cols = std::clamp(trajectory_.UsedCols(), 1, FG::kMaxGridCols);
  // Draw-domain rows = the ACTIVE formation (spec): the current wave's convoy rows, never the
  // screen capacity. Deriving rows from the window geometry let a warp land -- and permanently ...
  const int used_rows = std::clamp(formation_rows, 1, FG::kMaxGridRows);

  const auto& metrics = trajectory_.Metrics();
  const auto decision = FG::ComputeRelativisticWarp(
      origin_col, origin_row, used_cols, used_rows,
      player_x, player_y, trajectory_.BaseCruise(), trajectory_.StageCycle(),
      metrics.scale, metrics.win_w, metrics.win_h,
      occupancy, trajectory_.GetRng());

  ExecuteWarp(decision);
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
  // Rotation render-interpolation baseline: captured BEFORE this tick mutates
  // angle_/spin_angle_, mirroring previous_position for the draw-time lerp.
  prev_angle_total_ = angle_ + spin_angle_;
  // Keep the final landing step inside the warp movement budget even though
  // is_warping_ is cleared as soon as the cached destination is reached.
  const bool warp_transit_this_frame = is_warping_;

  const float s = trajectory_.Metrics().scale;
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
  // The classic post-dive wrap-around (attacking -> joining) repositions the diver off-screen above
  // the top edge in ONE intentional frame so it re-enters from the top. It is the armada-wide ...
  bool dive_wraparound = false;
  if (is_warping_) {
    RecordWarpGhost();
    const float warp_step = GameRules::SpecialEntities::kAlien14WarpSpeedPixels * s;
    // Priority 4b: consume strictly the cached destination; never re-sample per frame (prevents jitter)
    const Coord dest = warp_dest_;
    const float diff_x = static_cast<float>(dest.x) - transform_.position.x;
    const float diff_y = static_cast<float>(dest.y) - transform_.position.y;
    const float diff_dist = std::sqrt(diff_x * diff_x + diff_y * diff_y);

    const bool arrived = (diff_dist <= warp_step);
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
      transform_.position = simulation::AdvanceToward(
          transform_.position, dest.ToVec2f(), warp_step);
    }
    target_x = transform_.position.x;
    target_y = transform_.position.y;
  } else {
    const Trajectory::stage_t prev_stage = trajectory_.Stage();
    trajectory_.NextPositionF(transform_.position.x, transform_.position.y, scaled_vel, target_x, target_y);
    dive_wraparound = (prev_stage == Trajectory::attacking &&
                       trajectory_.Stage() == Trajectory::joining);
    if (trajectory_.Stage() == Trajectory::cruising) {
      if (prev_stage != Trajectory::cruising && attack_wait_timer_ <= 0) {
        attack_wait_timer_ = trajectory_.UniformInt(GameRules::Fleet::kMinAttackWaitFrames,
                                                    trajectory_.GetMaxAttackWaitFrames());
      }
    }
  }

  // Delta Jump Guard: the transit frame includes its final landing step, so clearing is_warping_
  // cannot clamp the cached destination at high scales. Outside transit, scaled_vel is the ...
  const float jump_x = target_x - transform_.previous_position.x;
  const float jump_y = target_y - transform_.previous_position.y;
  const float max_axis = std::max(std::abs(jump_x), std::abs(jump_y));
  if (!is_warping_ && !warp_transit_this_frame && max_axis > scaled_vel) {
    if (has_warp_property_ && !dive_wraparound) {
      const Vec2f clamped = simulation::ClampStepToBudget(
          transform_.previous_position, Vec2f(target_x, target_y), scaled_vel);
      target_x = clamped.x;
      target_y = clamped.y;
    } else {
      transform_.previous_position = Vec2f(target_x, target_y);
    }
  }

  transform_.position = Vec2f(target_x, target_y);
  MoveTo(Coord(transform_.position));

  const float dx = target_x - transform_.previous_position.x;
  const float dy = target_y - transform_.previous_position.y;
  const float move_dist = std::sqrt(dx * dx + dy * dy);

  if (move_dist > 1e-3f) {
    // Desired hull attitude: upright while docked (the 1 px/frame formation drift must never tilt
    // the ship), otherwise the actual travel direction of this tick -- a single source, so the ...
    const float target_angle =
        (trajectory_.Stage() == Trajectory::cruising)
            ? 0.0f
            : std::atan2(dy, dx) * (180.0f / Pi) - 90.0f;

    // Angular partitioning (pure helper, unit-tested headless): every 60 Hz tick consumes an
    // ease-out slice of the remaining arc -- proportional when far, at least the convergence ...
    angle_ = simulation::AdvanceTurnDeg(
        angle_, target_angle, GameRules::Fleet::kTurnResponseFactor,
        GameRules::Fleet::kTurnSnapThresholdDeg,
        GameRules::Fleet::kMaxTurnRateDeg);
  }

  if (spin_timer_ > 0) {
    --spin_timer_;
    spin_angle_ = kSpinAngleTable[static_cast<std::size_t>(std::max(0, spin_timer_))];
  } else if (!has_spun_ && trajectory_.Stage() == Trajectory::attacking) {
    if (trajectory_.Chance(1, GameRules::Fleet::kAlienSpinChanceDenominator)) {
      spin_timer_ = GameRules::Fleet::kAlienSpinAnimationFrames;
      has_spun_ = true;
    }
  }

  // Deterministic animation evolution on the fixed 60 Hz step (monitor-independent)
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
        // AGENT 6: the two ring-background auras above are still in the aura batch; flush them
        // BEFORE the direct SDL_RenderLines below so the painter's order (auras under the orbit ...
        Gfx::Inst().FlushAuraBatch();
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
          ring_pts[36] = ring_pts[0]; // Close the ring seamlessly
          SDL_RenderLines(rend, ring_pts, 37);
        }

        const float speed = GameRules::SpecialEntities::kAlien4ElectronAngularSpeed;
        // Counter-rotating electrons with asynchronous starting phases (no static mirroring)
        const float theta0 = electron_phase_ * speed + electron_offset0_;
        const float theta1 = -electron_phase_ * speed + electron_offset1_;

        // One tilt-rotation helper for both electrons (Deep DRY): the orbits
        // differ only in phase and tilt sign, so a single spelling cannot drift.
        const auto electron_pos = [&](float theta, float tilt_sin) noexcept {
          const float ox = rx * std::cos(theta);
          const float oy = ry * std::sin(theta);
          return Vec2f(ox * kTiltCos - oy * tilt_sin + static_cast<float>(render_pos.x),
                       ox * tilt_sin + oy * kTiltCos + static_cast<float>(render_pos.y));
        };
        const Vec2f e0 = electron_pos(theta0, kTiltSin);
        const Vec2f e1 = electron_pos(theta1, -kTiltSin);

        const float s = Gfx::Inst().Scale();
        const float electron_aura_radius = GameRules::SpecialEntities::kAlien4ElectronAuraRadiusPixels * s;
        const float electron_core_size = GameRules::SpecialEntities::kAlien4ElectronRadiusPixels * s;
        const Coord e0_coord(FastRound(e0.x), FastRound(e0.y));
        const Coord e1_coord(FastRound(e1.x), FastRound(e1.y));

        Gfx::Inst().DrawAura(e0_coord, electron_aura_radius, 60, 255, 100, 240);
        Gfx::Inst().DrawAura(e1_coord, electron_aura_radius, 0, 230, 255, 240);
        Gfx::Inst().DrawAura(e0_coord, electron_aura_radius * 0.5f, 220, 255, 200, 255);
        Gfx::Inst().DrawAura(e1_coord, electron_aura_radius * 0.5f, 200, 255, 255, 255);

        // AGENT 6: flush the four electron auras before the direct core
        // rects (auras under cores, identical to the immediate-mode order).
        Gfx::Inst().FlushAuraBatch();
        SDL_SetRenderDrawColor(rend, 255, 255, 255, 255);
        const SDL_FRect r0 = {e0.x - electron_core_size * 0.5f, e0.y - electron_core_size * 0.5f,
                              electron_core_size, electron_core_size};
        const SDL_FRect r1 = {e1.x - electron_core_size * 0.5f, e1.y - electron_core_size * 0.5f,
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
    } else if (warp_cooldown_timer_ > 0) {
      // Visual recharge (activation effect): a fading cyan halo marks the 1.4 s cooldown started
      // when the jump was consumed. Purely cosmetic feedback -- the trigger gate never reads ...
      const int recharge_frames = std::max(1, FastRound(
          GameRules::SpecialEntities::kAlien14WarpCooldownSeconds *
          GameRules::Simulation::kSimulationFrequencyHz));
      const float charge = static_cast<float>(warp_cooldown_timer_) /
                           static_cast<float>(recharge_frames);
      Gfx::Inst().DrawAura(render_pos, aura_rad * (0.9f + 0.5f * charge),
                           0, 160, 255, static_cast<uint8_t>(70.0f * charge));
    }
  }

  if (texture_id_ == TextureId::Alien4 && is_alpha_decayed_) {
    Gfx::Inst().DrawAura(render_pos, w * 1.1f, 50, 255, 120, 150);
  }

  // Smooth turns on any refresh rate: the hull angle lerps (shortest arc)
  // between physics ticks exactly like the position -- no extra FPS needed.
  const float render_angle = InterpolatedAngle(alpha) + extra_angle;
  if (object_.scale != 1.0f) {
    pix->DrawSized(render_pos, Width(), Height(), render_angle);
  } else {
    pix->DrawF(interp, render_angle);
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
  // Warp destinations are pixel-space state and must follow resize changes
  // so a mid-warp Alien14 keeps a valid target.
  warp_dest_.x = FastRound(static_cast<float>(warp_dest_.x) * rx);
  warp_dest_.y = FastRound(static_cast<float>(warp_dest_.y) * ry);
  Coord p = object_.transform.position;
  p.x = FastRound(static_cast<double>(p.x) * static_cast<double>(rx));
  p.y = FastRound(static_cast<double>(p.y) * static_cast<double>(ry));
  object_.transform.position = p;
  UpdateAABB();
  // Re-dock cruising aliens after resize, but let a mid-warp Alien14 finish
  // interpolating to its rescaled warp destination.
  if (trajectory_.Stage() == Trajectory::cruising && !is_warping_) {
    Coord target = trajectory_.CruiseTarget();
    transform_.position = target.ToVec2f();
    transform_.previous_position = transform_.position;
    MoveTo(target);
  }
}

Coord Alien::CannonPosition() const {
  Coord pos(Position());
  pos.y += (Height() + 1) / 2;  // ceil(half height): muzzle just below the hull
  return pos;
}
