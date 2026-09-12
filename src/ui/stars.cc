#include "stars.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <random>

#include "config.h"
#include "gfxinterface.h"

namespace {
std::mt19937& GetCosmicRng() {
  static std::random_device rd;
  static std::mt19937 rng(rd());
  return rng;
}
}  // namespace

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
  // Alternates or randomizes astrophysical cosmic profiles per stage
  const int mod = (level - 1) % 3;
  switch (mod) {
    case 0:
      current_profile_ = SectorProfile::CauchyFilament;
      break;
    case 1:
      current_profile_ = SectorProfile::SpiralArms;
      break;
    case 2:
      current_profile_ = SectorProfile::LaplaceDisk;
      break;
    default:
      current_profile_ = SectorProfile::CauchyFilament;
      break;
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
      const float spike_h = std::exp(-std::abs(dy) / 0.70f) *
                            std::exp(-std::abs(dx) / 7.0f) * 0.75f;
      const float spike_v = std::exp(-std::abs(dx) / 0.70f) *
                            std::exp(-std::abs(dy) / 7.0f) * 0.75f;

      const float intensity =
          std::clamp(core + halo + spike_h + spike_v, 0.0f, 1.0f);
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

  constexpr double kBaseArea = 1280.0 * 720.0;
  const double current_area = static_cast<double>(w) * static_cast<double>(h);
  const double linear_scale = std::sqrt(current_area / kBaseArea);

  const size_t target = static_cast<size_t>(std::round(240.0 * linear_scale));
  return std::clamp<size_t>(target, 180, 850);
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
    const float rx =
        static_cast<float>(cur_w) / static_cast<float>(last_window_w_);
    const float ry =
        static_cast<float>(cur_h) / static_cast<float>(last_window_h_);
    OnResize(rx, ry);
  }

  last_window_w_ = cur_w;
  last_window_h_ = cur_h;
}

float StarsFields::SampleSectorX(int win_w) {
  auto& rng = GetCosmicRng();
  std::uniform_real_distribution<float> dist_uni(0.0f, 1.0f);

  // 15% uniform isotropic cosmic dispersion across all sectors
  if (dist_uni(rng) < 0.15f) {
    return dist_uni(rng) * static_cast<float>(win_w);
  }

  float norm_x = 0.5f;

  switch (current_profile_) {
    case SectorProfile::CauchyFilament: {
      // Cauchy Distribution: Central galactic filament with heavy tails
      std::cauchy_distribution<float> cauchy_dist(0.0f, 0.38f);
      const float raw_c = cauchy_dist(rng);
      norm_x = 0.5f + 0.5f * std::tanh(raw_c);
      break;
    }
    case SectorProfile::SpiralArms: {
      // Lin-Shu Bimodal Distribution: Two galactic spiral arms flanking the
      // player
      std::normal_distribution<float> left_arm(0.24f, 0.09f);
      std::normal_distribution<float> right_arm(0.76f, 0.09f);
      norm_x = (dist_uni(rng) < 0.5f) ? left_arm(rng) : right_arm(rng);
      norm_x = std::clamp(norm_x, 0.02f, 0.98f);
      break;
    }
    case SectorProfile::LaplaceDisk: {
      // Freeman's Exponential Galactic Disk (Laplace distribution)
      const float u = dist_uni(rng) - 0.5f;
      const float sgn = (u < 0.0f) ? -1.0f : 1.0f;
      norm_x = 0.50f - 0.18f * sgn *
                           std::log(std::max(1e-5f, 1.0f - 2.0f * std::abs(u)));
      norm_x = std::clamp(norm_x, 0.02f, 0.98f);
      break;
    }
  }

  return norm_x * static_cast<float>(win_w);
}

void StarsFields::RespawnStar(Star& s, bool random_initial_y) {
  auto& rng = GetCosmicRng();
  const int win_w = std::max(1, Gfx::Inst().WindowWidth());
  const int win_h = std::max(1, Gfx::Inst().WindowHeight());

  s.x = SampleSectorX(win_w);

  if (random_initial_y) {
    std::uniform_real_distribution<float> dist_y(0.0f,
                                                 static_cast<float>(win_h));
    s.y = dist_y(rng);
  } else {
    s.y = 0.0f;
  }

  std::uniform_real_distribution<float> dist_uni(0.0f, 1.0f);
  const float layer_val = dist_uni(rng);

  if (layer_val < 0.82f) {
    // STAR TYPE 2 (82%): Soft twinkling diamond cross
    s.type = StarType::MidField;
    // REDUCED SPEED: 0.25 to 0.45 px/frame (very gentle, serene drift)
    s.base_speed = 0.25f + dist_uni(rng) * 0.20f;
    s.base_color = static_cast<uint8_t>(170 + dist_uni(rng) * 55.0f);
  } else {
    // STAR TYPE 3 (18%): Radiant optical diffraction star flare
    s.type = StarType::Foreground;
    // REDUCED SPEED: 0.45 to 0.70 px/frame (majestic slow glide)
    s.base_speed = 0.45f + dist_uni(rng) * 0.25f;
    s.base_color = static_cast<uint8_t>(235 + dist_uni(rng) * 20.0f);
  }

  std::uniform_real_distribution<float> dist_twinkle(0.0f, 6.283185f);
  s.twinkle_phase = dist_twinkle(rng);
  s.twinkle_speed = 0.03f + dist_uni(rng) * 0.05f;
}

void StarsFields::Scroll() {
  CheckWindowResize();

  const int win_h = std::max(1, Gfx::Inst().WindowHeight());
  const int max_active =
      Config::Instance().ScaleDetails(static_cast<int>(stars_.size()));

  // Salto hiperespacial sustentado durante a fanfarra, seguido por
  // desaceleracao suave
  if (warp_timer_ > 0) {
    --warp_timer_;
    const float progress = 1.0f - (static_cast<float>(warp_timer_) /
                                   static_cast<float>(warp_duration_));

    // Primeiros 65% do tempo: velocidade hiperespacial maxima sustentada
    if (progress < 0.65f) {
      warp_factor_ = 9.0f;
    } else {
      // Ultimos 35%: desaceleracao suave em cosseno ate velocidade de cruzeiro
      const float decel_phase = (progress - 0.65f) / 0.35f;
      const float ease_out = 0.5f + 0.5f * std::cos(decel_phase * 3.14159265f);
      warp_factor_ = 1.0f + 8.0f * ease_out;
    }
  } else if (warp_factor_ > 1.0f) {
    warp_factor_ += (1.0f - warp_factor_) * 0.05f;
    if (warp_factor_ < 1.02f) warp_factor_ = 1.0f;
  }

  for (int i = 0; i < max_active; ++i) {
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

  const int max_active =
      Config::Instance().ScaleDetails(static_cast<int>(stars_.size()));
  const bool is_warping = (warp_factor_ > 1.25f);
  const float scale_dpi = std::clamp(Gfx::Inst().Scale(), 0.9f, 1.4f);

  for (int i = 0; i < max_active; ++i) {
    const auto& s = stars_[i];
    const float twinkle = 0.85f + 0.15f * std::sin(s.twinkle_phase);
    const uint8_t c = static_cast<uint8_t>(
        std::clamp(static_cast<float>(s.base_color) * twinkle, 0.0f, 255.0f));

    if (is_warping) {
      // Relativistic light streaks during wave transition
      const float streak_len = s.base_speed * (warp_factor_ * 5.0f);
      const float start_y = std::max(0.0f, s.y - streak_len);
      SDL_SetRenderDrawColor(renderer, c, c,
                             static_cast<uint8_t>(std::min(255, c + 35)), 255);
      SDL_RenderLine(renderer, s.x, start_y, s.x, s.y);
    } else {
      switch (s.type) {
        case StarType::MidField: {
          // STAR TYPE 2: Soft diamond cross with active twinkle
          const float mid_twinkle =
              0.70f + 0.30f * std::sin(s.twinkle_phase * 1.35f);
          const uint8_t core_c = static_cast<uint8_t>(std::clamp(
              static_cast<float>(s.base_color) * mid_twinkle, 0.0f, 255.0f));
          const uint8_t halo_c =
              static_cast<uint8_t>(core_c * (0.35f + 0.20f * mid_twinkle));

          // Bright center core
          SDL_SetRenderDrawColor(renderer, core_c, core_c,
                                 std::min(255, core_c + 15), 255);
          SDL_RenderPoint(renderer, s.x, s.y);

          // Twinkling diamond arms
          SDL_SetRenderDrawColor(renderer, halo_c, halo_c, halo_c, halo_c);
          SDL_RenderPoint(renderer, s.x - 1.0f, s.y);
          SDL_RenderPoint(renderer, s.x + 1.0f, s.y);
          SDL_RenderPoint(renderer, s.x, s.y - 1.0f);
          SDL_RenderPoint(renderer, s.x, s.y + 1.0f);
          break;
        }

        case StarType::Foreground:
          // STAR TYPE 3: Radiant optical diffraction flare with 4 spikes and
          // glowing halo
          if (star_flare_tex_) {
            const float flare_size = (15.0f + 3.0f * twinkle) * scale_dpi;
            const SDL_FRect dst = {s.x - flare_size * 0.5f,
                                   s.y - flare_size * 0.5f, flare_size,
                                   flare_size};
            SDL_SetTextureColorMod(star_flare_tex_, c, c,
                                   static_cast<uint8_t>(std::min(255, c + 20)));
            SDL_SetTextureAlphaMod(star_flare_tex_,
                                   static_cast<uint8_t>(twinkle * 240.0f));
            SDL_RenderTexture(renderer, star_flare_tex_, nullptr, &dst);
          } else {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderPoint(renderer, s.x, s.y);
          }
          break;
      }
    }
  }
}
