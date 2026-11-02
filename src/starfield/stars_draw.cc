// Shell-side half of the starfield (RUST.md Fatia 6): everything that touches
// SDL or the live Gfx singleton, physically split from stars.cc exactly like
// the actors/managers/explosion draw translations. stars.cc keeps the golden-
// vector logic and links SDL-free into the dump_vectors tool.
#include "stars.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "config.h"
#include "constants.h"
#include "gfxinterface.h"
#include "hitch_monitor.h"

namespace {
// Precalculated quad index buffer: static constexpr in .rodata
constexpr size_t kMaxStarQuads = GameRules::Starfield::kMaxStarQuadCount;
constexpr auto s_quad_indices = []() {
  std::array<int, kMaxStarQuads * 6> indices{};
  for (size_t k = 0; k < kMaxStarQuads; ++k) {
    const int base = static_cast<int>(k * 4);
    const size_t out = k * 6;
    indices[out + 0] = base + 0;
    indices[out + 1] = base + 1;
    indices[out + 2] = base + 2;
    indices[out + 3] = base + 0;
    indices[out + 4] = base + 2;
    indices[out + 5] = base + 3;
  }
  return indices;
}();
}  // namespace

namespace {
// Bind the star-flare release hook once per process; the inline destructor
// calls through it, so headless TUs never link an SDL symbol (same contract
// as the Pix texture deleter).
const struct StarFlareHookBinder {
  StarFlareHookBinder() { StarsFields::SetFlareDeleter(&SDL_DestroyTexture); }
} star_flare_hook_binder;
}  // namespace

StarsFields::StarsFields()
    : StarsFields(Gfx::Inst().WindowWidth(), Gfx::Inst().WindowHeight()) {}

void StarsFields::CheckWindowResize() {
  const int cur_w = Gfx::Inst().WindowWidth();
  const int cur_h = Gfx::Inst().WindowHeight();
  if (last_window_w_ > 0 && last_window_h_ > 0 &&
      (cur_w != last_window_w_ || cur_h != last_window_h_)) {
    const float rx = static_cast<float>(cur_w) / static_cast<float>(last_window_w_);
    const float ry = static_cast<float>(cur_h) / static_cast<float>(last_window_h_);
    // The mirror updates BEFORE OnResize: the density recalculation inside it
    // reads the tracked viewport, which now equals the live window.
    last_window_w_ = cur_w;
    last_window_h_ = cur_h;
    OnResize(rx, ry);
  }
  last_window_w_ = cur_w;
  last_window_h_ = cur_h;
}

void StarsFields::InitFlareTexture(SDL_Renderer* renderer) {
  if (star_flare_tex_ || !renderer) return;
  constexpr int kSize = 64;
  SDL_Surface* surf = SDL_CreateSurface(kSize, kSize, SDL_PIXELFORMAT_RGBA32);
  if (!surf) return;
  auto* pixels = static_cast<uint8_t*>(surf->pixels);
  constexpr float kCenter = 31.5f;
  for (int py = 0; py < kSize; ++py) {
    for (int px = 0; px < kSize; ++px) {
      float dx = static_cast<float>(px) - kCenter;
      float dy = static_cast<float>(py) - kCenter;
      float dist = std::sqrt(dx * dx + dy * dy);
      float dist2 = dist * dist;
      float core = std::exp(-dist2 / GameRules::Starfield::kFlareCoreSigma2) * GameRules::Starfield::kFlareCoreGain;
      float halo = std::exp(-dist / GameRules::Starfield::kFlareHaloSigma) * GameRules::Starfield::kFlareHaloGain;
      float angle = std::atan2(dy, dx);
      float spike = std::pow(std::abs(std::cos(angle * 2.0f)), 32.0f) * std::exp(-dist / 12.0f) * 0.6f;
      spike += std::pow(std::abs(std::cos(angle * 2.0f + 1.5708f)), 32.0f) * std::exp(-dist / 12.0f) * 0.6f;
      float intensity = std::clamp(core + halo + spike, 0.0f, 1.0f);
      intensity *= std::clamp(1.0f - dist / 31.0f, 0.0f, 1.0f);
      int idx = py * surf->pitch + px * 4;
      pixels[idx + 0] = 255;
      pixels[idx + 1] = 255;
      pixels[idx + 2] = 255;
      pixels[idx + 3] = static_cast<uint8_t>(intensity * 255.0f);
    }
  }
  star_flare_tex_ = SDL_CreateTextureFromSurface(renderer, surf);
  SDL_DestroySurface(surf);
  if (star_flare_tex_) {
    SDL_SetTextureBlendMode(star_flare_tex_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(star_flare_tex_, SDL_SCALEMODE_LINEAR);
  }
}

void StarsFields::Draw() {
  HITCH_SCOPE("StarsFields::Draw");
  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;
  if (!star_flare_tex_) InitFlareTexture(renderer);
  // CheckDensityChange() already runs inside Scroll(), which always precedes Draw().

  const int density_level = config_ ? config_->DetailsLevel() : 2;
  if (density_level == 0) return;

  const size_t max_active = active_stars_;
  const bool is_warping = (warp_factor_ > 1.25f);
  const float scale_dpi = std::clamp(Gfx::Inst().Scale(),
      GameRules::Starfield::kStarScaleDpiMin,
      GameRules::Starfield::kStarScaleDpiMax);

  // Hyperspace warp: build all streaks using precalculated static quad indices.
  // Reuses s_quad_indices without per-frame vector allocations or pushes.
  if (is_warping) {
    static std::vector<SDL_Vertex> warp_vertices;
    if (warp_vertices.size() < max_active * 4) {
      warp_vertices.resize(max_active * 4);
    }

    constexpr float kStreakWidth = 1.0f;
    size_t warp_quad_count = 0;

    for (size_t i = 0; i < max_active; ++i) {
      const auto& s = stars_[i];
      const float streak = s.base_speed * (warp_factor_ * 6.0f);
      const float top = std::max(0.0f, s.y - streak);
      const float left = s.x - kStreakWidth * 0.5f;
      const float right = s.x + kStreakWidth * 0.5f;

      const SDL_FColor color{
          static_cast<float>(s.r) / 255.0f,
          static_cast<float>(s.g) / 255.0f,
          static_cast<float>(s.b) / 255.0f,
          1.0f};

      const size_t bv = warp_quad_count * 4;
      warp_vertices[bv + 0] = {{left, top}, color, {0.0f, 0.0f}};
      warp_vertices[bv + 1] = {{right, top}, color, {0.0f, 0.0f}};
      warp_vertices[bv + 2] = {{right, s.y}, color, {0.0f, 0.0f}};
      warp_vertices[bv + 3] = {{left, s.y}, color, {0.0f, 0.0f}};
      ++warp_quad_count;
    }

    if (warp_quad_count > 0) {
      SDL_RenderGeometry(renderer, nullptr, warp_vertices.data(),
                         static_cast<int>(warp_quad_count * 4),
                         s_quad_indices.data(),
                         static_cast<int>(warp_quad_count * 6));
    }
    return;
  }

    // Unified vertex buffer: merges MidField and Foreground into a single geometry batch.
  // Cuts GPU draw calls and cuts static BSS memory allocation in half.
  static constexpr float kMidColors[4][3] = {
      {185.0f / 255.0f, 220.0f / 255.0f, 255.0f / 255.0f},  // Blue
      {245.0f / 255.0f, 250.0f / 255.0f, 255.0f / 255.0f},  // White
      {255.0f / 255.0f, 245.0f / 255.0f, 190.0f / 255.0f},  // Yellow
      {255.0f / 255.0f, 175.0f / 255.0f, 120.0f / 255.0f},  // OrangeRed
  };
  static std::array<SDL_Vertex, StarsFields::kStarPoolCapacity * 4> unified_star_verts{};
  size_t total_quad_count = 0;

  static constexpr struct { float r, g, b; } kSpectralColors[4] = {
      {185.0f / 255.0f, 225.0f / 255.0f, 1.0f},
      {250.0f / 255.0f, 250.0f / 255.0f, 1.0f},
      {1.0f, 235.0f / 255.0f, 180.0f / 255.0f},
      {1.0f, 160.0f / 255.0f, 110.0f / 255.0f}};

  for (size_t i = 0; i < max_active; ++i) {
    const auto& s = stars_[i];
    namespace SF = GameRules::Starfield;
    const uint8_t phase_idx = static_cast<uint8_t>(s.twinkle_phase >> 8u);
    const float tw = static_cast<float>(s.twinkle_alpha) * (1.0f / 255.0f);
    const float pulse = TwinklePulse(phase_idx);

    if (s.type == StarType::MidField) {
      // Distant star: soft Airy disk, never a single pixel.
      // Diameter = 2*(min + pulse*amp)*dpi  →  ~6.4 .. 11.2 px at 1× DPI.
      const float hs = (SF::kMidFieldHalfSizeMinPx +
                        (SF::kMidFieldHalfSizeMaxPx - SF::kMidFieldHalfSizeMinPx) *
                            pulse) *
                       scale_dpi;
      const auto& mc = kMidColors[static_cast<size_t>(s.spectral)];
      const float alpha = std::max(SF::kMidFieldAlphaFloor, tw);
      const SDL_FColor color{mc[0], mc[1], mc[2], alpha};
      const size_t bv = total_quad_count * 4;
      unified_star_verts[bv + 0] = {{s.x - hs, s.y - hs}, color, {0.0f, 0.0f}};
      unified_star_verts[bv + 1] = {{s.x + hs, s.y - hs}, color, {1.0f, 0.0f}};
      unified_star_verts[bv + 2] = {{s.x + hs, s.y + hs}, color, {1.0f, 1.0f}};
      unified_star_verts[bv + 3] = {{s.x - hs, s.y + hs}, color, {0.0f, 1.0f}};
      ++total_quad_count;
    } else {
      // Nearby star: larger flare; diffraction spikes breathe with the pulse.
      const float star_size = (SF::kForegroundSizeMinPx +
                               (SF::kForegroundSizeMaxPx - SF::kForegroundSizeMinPx) *
                                   pulse) *
                              scale_dpi;
      const float hsz = star_size * 0.5f;
      const float left = s.x - hsz;
      const float top = s.y - hsz;
      const float af = std::max(static_cast<float>(SF::kForegroundAlphaFloor),
                                static_cast<float>(s.twinkle_alpha)) *
                       (1.0f / 255.0f);

      const auto& sc = kSpectralColors[static_cast<size_t>(s.spectral)];
      const SDL_FColor color{sc.r, sc.g, sc.b, af};

      const size_t v_idx = total_quad_count * 4;
      unified_star_verts[v_idx + 0] = {{left, top}, color, {0.0f, 0.0f}};
      unified_star_verts[v_idx + 1] = {{left + star_size, top}, color, {1.0f, 0.0f}};
      unified_star_verts[v_idx + 2] = {{left + star_size, top + star_size}, color, {1.0f, 1.0f}};
      unified_star_verts[v_idx + 3] = {{left, top + star_size}, color, {0.0f, 1.0f}};

      ++total_quad_count;
    }
  }

  // Single consolidated geometry draw call for the entire starfield
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  if (total_quad_count > 0) {
    SDL_RenderGeometry(renderer, star_flare_tex_, unified_star_verts.data(),
                       static_cast<int>(total_quad_count * 4),
                       s_quad_indices.data(),
                       static_cast<int>(total_quad_count * 6));
  }
}
