// Render half of the particle system (Slice 3 core/shell split): the SDL
// point-batch renderer lives here, so explosion.cc links without SDL.
#include "explosion.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "constants.h"
#include "math_types.h"

void ExplosionsManager::Draw() const {
  if (active_count_ == 0) return;
  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;

  // Lean fixed point batches: the total point budget reserved by the explosion renderer (SSOT:
  // kExplosionPointBatchCapacity) is split evenly across the four color bands, keeping the ...
  constexpr size_t kColorBands = 4;
  constexpr size_t kCap =
      GameRules::Combat::kExplosionPointBatchCapacity / kColorBands;
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

