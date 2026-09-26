#include "explosion.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "config.h"
#include "constants.h"
#include "formation_grid.h"
#include "math_types.h"

void ExplosionsManager::Clear() noexcept {
  for (size_t i = 0; i < active_count_; ++i) {
    pool_[active_indices_[i]].active = false;
  }
  active_count_ = 0;
  free_count_ = kMaxParticles;
  for (size_t i = 0; i < kMaxParticles; ++i) {
    free_indices_[i] = static_cast<uint16_t>(i);
  }
}

void ExplosionsManager::OnResize(float rx, float ry) noexcept {
  for (size_t i = 0; i < active_count_; ++i) {
    auto& p = pool_[active_indices_[i]];
    p.pos.x *= rx;
    p.pos.y *= ry;
    p.vel.x *= rx;
    p.vel.y *= ry;
  }
}

void ExplosionsManager::Add(Coord pos, Coord speed, uint8_t r, uint8_t g, uint8_t b, int duration) {
  const int scaled_details =
      config_.ScaleDetails(GameRules::Combat::kExplosionDebrisScaleFactor * duration);
  const int count = std::clamp(scaled_details,
                               GameRules::Combat::kExplosionDebrisMinParticles,
                               GameRules::Combat::kExplosionDebrisMaxParticles);

  // Raio físico final máximo rigorosamente delimitado pela fuselagem da nave
  const float max_radius = static_cast<float>(GameRules::Fleet::Width()) *
                           GameRules::Combat::kExplosionDebrisRadiusMultiplier;

  int spawned = 0;
  while (free_count_ > 0 && spawned < count) {
    const uint16_t idx = free_indices_[--free_count_];
    auto& p = pool_[idx];
    if (true) {
      // Ângulo polar uniforme (explosão perfeitamente circular, sem cantos quadrados)
      const float angle = rng_.UniformFloat(0.0f, 6.2831853f);
      const float cos_a = std::cos(angle);
      const float sin_a = std::sin(angle);

      // Ponto de nascimento confinado ao núcleo da nave (até 25% do raio)
      const float r_init = rng_.UniformFloat(
          0.0f, max_radius * GameRules::Combat::kExplosionInitialRadiusFactor);
      p.pos = Vec2f(static_cast<float>(pos.x) + cos_a * r_init,
                    static_cast<float>(pos.y) + sin_a * r_init);

      p.life = p.max_life = static_cast<uint16_t>(duration + rng_.UniformInt(-3, 3));
      p.inv_max_life = (p.max_life > 0) ? (1.0f / static_cast<float>(p.max_life)) : 0.0f;

      // Compile-time precomputed geometric decay table: sum_{step=0}^{k-1} (drag)^step.
      // Evaluated entirely at compilation into .rodata; eliminates runtime std::pow() calls.
      static constexpr auto kDecayTable = []() {
        std::array<float, 128> table{};
        constexpr float kDragConst = GameRules::Combat::kExplosionDrag;
        for (size_t k = 1; k < 128; ++k) {
          float sum = 0.0f;
          float term = 1.0f;
          for (size_t step = 0; step < k; ++step) {
            sum += term;
            term *= kDragConst;
          }
          table[k] = sum;
        }
        return table;
      }();

      const size_t life_clamped = std::min<size_t>(static_cast<size_t>(p.life), 127u);
      const float decay_sum = kDecayTable[life_clamped];

      // Distância de expansão: varia entre o núcleo e o limite exato de max_radius
      const float target_expansion =
          (max_radius - r_init) *
          rng_.UniformFloat(GameRules::Combat::kExplosionExpansionMinFactor, 1.0f);
      const float v_mag =
          (decay_sum > GameRules::Combat::kExplosionDecayEpsilon)
              ? (target_expansion / decay_sum)
              : 1.0f;

      const float residual_vx = static_cast<float>(speed.x) * 0.15f;
      const float residual_vy = static_cast<float>(speed.y) * 0.15f;

      p.vel = Vec2f(cos_a * v_mag + residual_vx,
                    sin_a * v_mag + residual_vy);

      p.r = r;
      p.g = g;
      p.b = b;
      p.a = 255;
      p.active = true;

      active_indices_[active_count_++] = static_cast<uint16_t>(idx);
      ++spawned;
    }
  }
}

void ExplosionsManager::TriggerScreenWideNova(Coord epicenter, int win_w, int win_h) {
  const float s = Gfx::Inst().Scale();
  Add(epicenter, Coord(0, 0), 255, 255, 240, 75);
  Add(epicenter, Coord(0, static_cast<int>(-3.0f * s)), 255, 180, 40, 65);
  Add(epicenter, Coord(0, static_cast<int>(3.0f * s)), 255, 50, 20, 65);

  const float max_speed = 10.0f * s;
  const float min_speed = 2.0f * s;

  while (free_count_ > 0 && active_count_ < kMaxParticles) {
    const uint16_t idx = free_indices_[--free_count_];
    auto& p = pool_[idx];
    if (true) {
      const float ang = rng_.UniformFloat(0.0f, 6.2831853f);
      const float spd = rng_.UniformFloat(min_speed, max_speed);
      p.pos = epicenter.ToVec2f();
      p.vel = Vec2f(std::cos(ang) * spd, std::sin(ang) * spd);
      p.r = 255;
      p.g = static_cast<uint8_t>(rng_.UniformInt(100, 240));
      p.b = 30;
      p.a = 255;
      p.life = p.max_life = static_cast<uint16_t>(rng_.UniformInt(40, 75));
      p.inv_max_life = (p.max_life > 0) ? (1.0f / static_cast<float>(p.max_life)) : 0.0f;
      p.active = true;
      active_indices_[active_count_++] = static_cast<uint16_t>(idx);
    }
  }

  const int step_x = FastRound(220.0f * s);
  const int step_y = FastRound(180.0f * s);
  const int margin = FastRound(80.0f * s);
  for (int x = margin; x < win_w; x += step_x) {
    for (int y = margin; y < win_h; y += step_y) {
      Add(Coord(x + rng_.UniformInt(-static_cast<int>(30.0f * s), static_cast<int>(30.0f * s)),
                y + rng_.UniformInt(-static_cast<int>(30.0f * s), static_cast<int>(30.0f * s))),
          Coord(rng_.UniformInt(-2, 2), rng_.UniformInt(-2, 2)),
          255, 120, 20, 45);
    }
  }
}

void ExplosionsManager::Move() noexcept {
  if (active_count_ == 0) return;

  for (size_t i = 0; i < active_count_; ) {
    const uint16_t idx = active_indices_[i];
    auto& p = pool_[idx];
    p.pos += p.vel;
    p.vel *= GameRules::Combat::kExplosionDrag;

    if (p.life > 0) {
      --p.life;
      const float ratio = static_cast<float>(p.life) * p.inv_max_life;
      p.a = static_cast<uint8_t>(ratio * 255.0f);
      p.r = static_cast<uint8_t>(static_cast<float>(p.r) * 0.985f);
      p.g = static_cast<uint8_t>(static_cast<float>(p.g) * 0.955f);
      p.b = static_cast<uint8_t>(static_cast<float>(p.b) * 0.955f);
      ++i;
    } else {
      p.active = false;
      free_indices_[free_count_++] = idx;
      active_indices_[i] = active_indices_[--active_count_];
    }
  }
}

void ExplosionsManager::Draw() const {
  if (active_count_ == 0) return;
  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;

  // Lean fixed point batches: 8192 points per color band (reduces static RAM footprint by 768 KB)
  constexpr size_t kCap = 8192;
  static std::array<SDL_FPoint, kCap> pts_bright{};
  static std::array<SDL_FPoint, kCap> pts_orange{};
  static std::array<SDL_FPoint, kCap> pts_red{};
  static std::array<SDL_FPoint, kCap> pts_dark{};
  size_t n_bright = 0, n_orange = 0, n_red = 0, n_dark = 0;

  const float s = Gfx::Inst().Scale();

  auto emit = [](std::array<SDL_FPoint, kCap>& buf, size_t& n, float x, float y) noexcept {
    if (n >= kCap) return;
    buf[n++] = {x, y};
  };

  for (size_t i = 0; i < active_count_; ++i) {
    const auto& p = pool_[active_indices_[i]];
    if (!p.active || p.a == 0) continue;

    const float ratio = (p.max_life > 0)
                            ? (static_cast<float>(p.life) / static_cast<float>(p.max_life))
                            : 0.0f;
    std::array<SDL_FPoint, kCap>* target = nullptr;
    size_t* n = nullptr;
    if (ratio > 0.65f) {
      target = &pts_bright;
      n = &n_bright;
    } else if (ratio > 0.38f) {
      target = &pts_orange;
      n = &n_orange;
    } else if (ratio > 0.15f) {
      target = &pts_red;
      n = &n_red;
    } else {
      target = &pts_dark;
      n = &n_dark;
    }

    const float px = p.pos.x;
    const float py = p.pos.y;
    emit(*target, *n, px, py);
    emit(*target, *n, px + 1.0f, py);
    emit(*target, *n, px, py + 1.0f);
    if (s >= 0.80f) {
      emit(*target, *n, px + 1.0f, py + 1.0f);
    }
    if (s >= 1.20f) {
      emit(*target, *n, px - 1.0f, py);
      emit(*target, *n, px, py - 1.0f);
    }
    if (s >= 1.70f) {
      emit(*target, *n, px + 2.0f, py);
      emit(*target, *n, px, py + 2.0f);
      emit(*target, *n, px - 1.0f, py - 1.0f);
    }
  }

  if (n_bright > 0) {
    SDL_SetRenderDrawColor(renderer, 255, 245, 200, 255);
    SDL_RenderPoints(renderer, pts_bright.data(), static_cast<int>(n_bright));
  }
  if (n_orange > 0) {
    SDL_SetRenderDrawColor(renderer, 255, 145, 35, 220);
    SDL_RenderPoints(renderer, pts_orange.data(), static_cast<int>(n_orange));
  }
  if (n_red > 0) {
    SDL_SetRenderDrawColor(renderer, 230, 60, 25, 180);
    SDL_RenderPoints(renderer, pts_red.data(), static_cast<int>(n_red));
  }
  if (n_dark > 0) {
    SDL_SetRenderDrawColor(renderer, 120, 35, 20, 110);
    SDL_RenderPoints(renderer, pts_dark.data(), static_cast<int>(n_dark));
  }
}

