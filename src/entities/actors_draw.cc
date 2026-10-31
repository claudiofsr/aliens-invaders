// Render half of the alien actor (Slice 3 core/shell split): every Gfx/SDL
// consumer lives here, so actors.cc links without SDL (golden-vector tools).
#include "actors.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "constants.h"
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

}  // namespace

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

