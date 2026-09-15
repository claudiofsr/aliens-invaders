#include "stars.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <vector>

#include "config.h"
#include "game_rules.h"
#include "gfxinterface.h"

StarsFields::StarsFields() {
  last_window_w_ = std::max(1280, Gfx::Inst().WindowWidth());
  last_window_h_ = std::max(720, Gfx::Inst().WindowHeight());

  AdjustStarDensity();
  for (auto& s : stars_) {
    RespawnStar(s, true);
  }
}

StarsFields::~StarsFields() {
  if (star_flare_tex_) {
    SDL_DestroyTexture(star_flare_tex_);
    star_flare_tex_ = nullptr;
  }
}

void StarsFields::SelectDistributionForLevel(int level) noexcept {
  const int mod = (level - 1) % 3;
  switch (mod) {
    case 0: current_profile_ = SectorProfile::CauchyFilament; break;
    case 1: current_profile_ = SectorProfile::SpiralArms; break;
    case 2: current_profile_ = SectorProfile::LaplaceDisk; break;
    default: current_profile_ = SectorProfile::CauchyFilament; break;
  }
}

void StarsFields::InitFlareTexture(SDL_Renderer* renderer) {
  if (star_flare_tex_ || !renderer) return;

  constexpr int kSize = 31;
  SDL_Surface* surf = SDL_CreateSurface(kSize, kSize, SDL_PIXELFORMAT_RGBA32);
  if (!surf) return;

  auto* pixels = static_cast<uint8_t*>(surf->pixels);
  constexpr float kCenter = 15.0f;

  for (int py = 0; py < kSize; ++py) {
    for (int px = 0; px < kSize; ++px) {
      const float dx = static_cast<float>(px) - kCenter;
      const float dy = static_cast<float>(py) - kCenter;
      const float dist = std::sqrt(dx * dx + dy * dy);

      const float core = std::exp(-dist / 1.4f) * 1.6f;
      const float halo = std::exp(-dist / 4.2f) * 0.40f;
      const float spike_h = std::exp(-std::abs(dy) / 0.70f) * std::exp(-std::abs(dx) / 7.0f) * 0.75f;
      const float spike_v = std::exp(-std::abs(dx) / 0.70f) * std::exp(-std::abs(dy) / 7.0f) * 0.75f;

      const float intensity = std::clamp(core + halo + spike_h + spike_v, 0.0f, 1.0f);
      const uint8_t alpha = static_cast<uint8_t>(intensity * 255.0f);

      const int idx = py * surf->pitch + px * 4;
      pixels[idx + 0] = 255;
      pixels[idx + 1] = 255;
      pixels[idx + 2] = 255;
      pixels[idx + 3] = alpha;
    }
  }

  star_flare_tex_ = SDL_CreateTextureFromSurface(renderer, surf);
  SDL_DestroySurface(surf);

  if (star_flare_tex_) {
    SDL_SetTextureBlendMode(star_flare_tex_, SDL_BLENDMODE_BLEND);
  }
}

size_t StarsFields::CalculateTargetStarCount() const noexcept {
  const int w = std::max(640, Gfx::Inst().WindowWidth());
  const int h = std::max(480, Gfx::Inst().WindowHeight());

  const double kBaseArea = GameRules::Starfield::kBaseResolutionWidthPixels * GameRules::Starfield::kBaseResolutionHeightPixels;
  const double current_area = static_cast<double>(w) * static_cast<double>(h);
  const double linear_scale = std::sqrt(current_area / kBaseArea);

  const size_t target = static_cast<size_t>(std::round(GameRules::Starfield::kBaseStarCount * linear_scale));
  return std::clamp<size_t>(target, GameRules::Starfield::kMinStarsCount, GameRules::Starfield::kMaxStarsCount);
}

void StarsFields::AdjustStarDensity() {
  const size_t target_count = CalculateTargetStarCount();
  const size_t current_count = stars_.size();

  if (target_count > current_count) {
    stars_.resize(target_count);
    for (size_t i = current_count; i < target_count; ++i) {
      RespawnStar(stars_[i], true);
    }
  } else if (target_count < current_count) {
    stars_.resize(target_count);
  }
}

void StarsFields::OnResize(float rx, float ry) noexcept {
  if (rx <= 0.0f || ry <= 0.0f) return;

  for (auto& s : stars_) {
    s.x *= rx;
    s.y *= ry;
  }

  last_window_w_ = Gfx::Inst().WindowWidth();
  last_window_h_ = Gfx::Inst().WindowHeight();

  AdjustStarDensity();
}

void StarsFields::CheckWindowResize() noexcept {
  const int cur_w = Gfx::Inst().WindowWidth();
  const int cur_h = Gfx::Inst().WindowHeight();

  if (last_window_w_ > 0 && last_window_h_ > 0 &&
      (cur_w != last_window_w_ || cur_h != last_window_h_)) {
    const float rx = static_cast<float>(cur_w) / static_cast<float>(last_window_w_);
    const float ry = static_cast<float>(cur_h) / static_cast<float>(last_window_h_);
    OnResize(rx, ry);
  }

  last_window_w_ = cur_w;
  last_window_h_ = cur_h;
}

float StarsFields::SampleSectorX(int win_w) {
  if (rng_.UniformFloat(0.0f, 1.0f) < 0.15f) {
    return rng_.UniformFloat(0.0f, static_cast<float>(win_w));
  }

  float norm_x = 0.5f;
  switch (current_profile_) {
    case SectorProfile::CauchyFilament: {
      norm_x = 0.5f + 0.5f * std::tanh(rng_.Cauchy(0.0f, 0.38f));
      break;
    }
    case SectorProfile::SpiralArms: {
      norm_x = (rng_.UniformFloat(0.0f, 1.0f) < 0.5f)
                   ? rng_.UniformFloat(0.15f, 0.35f)
                   : rng_.UniformFloat(0.65f, 0.85f);
      break;
    }
    case SectorProfile::LaplaceDisk: {
      const float u = rng_.UniformFloat(0.0f, 1.0f) - 0.5f;
      const float sgn = (u < 0.0f) ? -1.0f : 1.0f;
      norm_x = 0.50f - 0.18f * sgn * std::log(std::max(1e-5f, 1.0f - 2.0f * std::abs(u)));
      break;
    }
  }

  norm_x = std::clamp(norm_x, 0.02f, 0.98f);
  return norm_x * static_cast<float>(win_w);
}

void StarsFields::RespawnStar(Star& s, bool random_initial_y) {
  const int win_w = std::max(1, Gfx::Inst().WindowWidth());
  const int win_h = std::max(1, Gfx::Inst().WindowHeight());

  s.x = SampleSectorX(win_w);
  if (random_initial_y) {
    s.y = rng_.UniformFloat(0.0f, static_cast<float>(win_h));
  } else {
    s.y = 0.0f;
  }

  const float layer_val = rng_.UniformFloat(0.0f, 1.0f);
  if (layer_val < GameRules::Starfield::kForegroundLayerThreshold) {
    s.type = StarType::MidField;
    s.base_speed = rng_.UniformFloat(0.25f, 0.45f);
    s.base_color = static_cast<uint8_t>(rng_.UniformInt(170, 225));
  } else {
    s.type = StarType::Foreground;
    s.base_speed = rng_.UniformFloat(0.45f, 0.70f);
    s.base_color = static_cast<uint8_t>(rng_.UniformInt(235, 255));
  }

  s.twinkle_phase = rng_.UniformFloat(0.0f, 6.283185f);
  s.twinkle_speed = rng_.UniformFloat(0.03f, 0.08f);
}

void StarsFields::Scroll() {
  CheckWindowResize();

  const int win_h = std::max(1, Gfx::Inst().WindowHeight());
  const int max_active = Config::Instance().ScaleDetails(static_cast<int>(stars_.size()));

  if (warp_timer_ > 0) {
    --warp_timer_;
    const float progress = 1.0f - (static_cast<float>(warp_timer_) / static_cast<float>(warp_duration_));
    if (progress < 0.65f) {
      warp_factor_ = GameRules::Progression::kHyperspaceWarpSpeedMultiplier;
    } else {
      const float decel_phase = (progress - 0.65f) / 0.35f;
      const float ease_out = 0.5f + 0.5f * std::cos(decel_phase * 3.14159265f);
      warp_factor_ = 1.0f + (GameRules::Progression::kHyperspaceWarpSpeedMultiplier - 1.0f) * ease_out;
    }
  } else if (warp_factor_ > 1.0f) {
    warp_factor_ += (1.0f - warp_factor_) * 0.05f;
    if (warp_factor_ < 1.02f) warp_factor_ = 1.0f;
  }

  for (size_t i = 0; i < static_cast<size_t>(max_active); ++i) {
    auto& s = stars_[i];
    s.y += s.base_speed * warp_factor_;
    s.twinkle_phase += s.twinkle_speed;

    if (s.y >= static_cast<float>(win_h)) {
      RespawnStar(s, false);
    }
  }
}

void StarsFields::Draw() {
  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;

  if (!star_flare_tex_) {
    InitFlareTexture(renderer);
  }

  const int max_active = Config::Instance().ScaleDetails(static_cast<int>(stars_.size()));
  const bool is_warping = (warp_factor_ > 1.25f);
  const float scale_dpi = std::clamp(Gfx::Inst().Scale(), 0.9f, 1.4f);

  static thread_local std::vector<SDL_FPoint> core_points;
  static thread_local std::vector<SDL_FPoint> halo_points;
  core_points.clear();
  halo_points.clear();

  for (size_t i = 0; i < static_cast<size_t>(max_active); ++i) {
    const auto& s = stars_[i];
    const float twinkle = 0.85f + 0.15f * std::sin(s.twinkle_phase);
    const uint8_t c = static_cast<uint8_t>(std::clamp(static_cast<float>(s.base_color) * twinkle, 0.0f, 255.0f));

    if (is_warping) {
      const float streak_len = s.base_speed * (warp_factor_ * 5.0f);
      const float start_y = std::max(0.0f, s.y - streak_len);
      SDL_SetRenderDrawColor(renderer, c, c, static_cast<uint8_t>(std::min(255, c + 35)), 255);
      SDL_RenderLine(renderer, s.x, start_y, s.x, s.y);
    } else {
      switch (s.type) {
        case StarType::MidField:
          core_points.push_back({s.x, s.y});
          halo_points.push_back({s.x - 1.0f, s.y});
          halo_points.push_back({s.x + 1.0f, s.y});
          halo_points.push_back({s.x, s.y - 1.0f});
          halo_points.push_back({s.x, s.y + 1.0f});
          break;

        case StarType::Foreground:
          if (star_flare_tex_) {
            const float flare_size = (15.0f + 3.0f * twinkle) * scale_dpi;
            const SDL_FRect dst = {s.x - flare_size * 0.5f, s.y - flare_size * 0.5f, flare_size, flare_size};
            SDL_SetTextureColorMod(star_flare_tex_, c, c, static_cast<uint8_t>(std::min(255, c + 20)));
            SDL_SetTextureAlphaMod(star_flare_tex_, static_cast<uint8_t>(twinkle * 240.0f));
            SDL_RenderTexture(renderer, star_flare_tex_, nullptr, &dst);
          }
          break;
      }
    }
  }

  if (!core_points.empty()) {
    SDL_SetRenderDrawColor(renderer, 240, 245, 255, 255);
    SDL_RenderPoints(renderer, core_points.data(), static_cast<int>(core_points.size()));
  }
  if (!halo_points.empty()) {
    SDL_SetRenderDrawColor(renderer, 100, 130, 160, 100);
    SDL_RenderPoints(renderer, halo_points.data(), static_cast<int>(halo_points.size()));
  }
}
