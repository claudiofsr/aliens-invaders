#include "stars.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "config.h"
#include "constants.h"
#include "gfxinterface.h"

namespace {
[[nodiscard]] inline float FastTwinkleValue(uint8_t index) noexcept {
  static const auto s_table = []() {
    std::array<float, GameRules::Starfield::kTwinkleTableSize> table{};
    for (size_t i = 0; i < table.size(); ++i) {
      const float angle = static_cast<float>(i) *
                          (6.28318530718f /
                           static_cast<float>(GameRules::Starfield::kTwinkleTableSize));
      table[i] = 0.85f + 0.15f * std::sin(angle);
    }
    return table;
  }();
  return s_table[index];
}
}  // namespace

StarsFields::StarsFields() {
  last_window_w_ = std::max(1280, Gfx::Inst().WindowWidth());
  last_window_h_ = std::max(720, Gfx::Inst().WindowHeight());
  AdjustStarDensity();
  for (auto& s : stars_) RespawnStar(s, true);
}

StarsFields::~StarsFields() {
  if (star_flare_tex_) {
    SDL_DestroyTexture(star_flare_tex_);
    star_flare_tex_ = nullptr;
  }
}

void StarsFields::SelectDistributionForLevel(int) noexcept {
  current_profile_ = SectorProfile::CauchyFilament;
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
      float core = std::exp(-dist2 / 9.0f) * 1.8f;
      float halo = std::exp(-dist / 9.0f) * 0.35f;
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
  if (star_flare_tex_) SDL_SetTextureBlendMode(star_flare_tex_, SDL_BLENDMODE_BLEND);
}

size_t StarsFields::CalculateTargetStarCount() const noexcept {
  const int density_level = config_ ? config_->DetailsLevel() : 2;
  if (density_level == 0) return 0;

  const int w = std::max(640, Gfx::Inst().WindowWidth());
  const int h = std::max(480, Gfx::Inst().WindowHeight());
  const double kBaseArea = static_cast<double>(GameRules::Starfield::kBaseResolutionWidthPixels) *
                           static_cast<double>(GameRules::Starfield::kBaseResolutionHeightPixels);
  const double current_area = static_cast<double>(w) * static_cast<double>(h);
  const double scale = std::sqrt(current_area / kBaseArea);
  size_t base_target = static_cast<size_t>(std::round(static_cast<double>(GameRules::Starfield::kBaseStarCount) * scale));

  // Escala canônica calibrada obtida diretamente do SSOT em constants.h
  const double factor = GameRules::Starfield::GetDensityFactor(density_level);

  size_t target = static_cast<size_t>(std::round(static_cast<double>(base_target) * factor));
  if (target == 0) return 0;
  return std::clamp<size_t>(target, GameRules::Starfield::kMinStarsCount, GameRules::Starfield::kMaxStarsCount * 2);
}

void StarsFields::AdjustStarDensity() {
  const size_t target = CalculateTargetStarCount();
  if (target > stars_.size()) {
    size_t old = stars_.size();
    stars_.resize(target);
    for (size_t i = old; i < target; ++i) RespawnStar(stars_[i], true);
  } else if (target < stars_.size()) {
    stars_.resize(target);
  }
}

void StarsFields::OnResize(float rx, float ry) noexcept {
  if (rx <= 0.0f || ry <= 0.0f) return;
  int cur_w = Gfx::Inst().WindowWidth();
  int cur_h = Gfx::Inst().WindowHeight();
  int prev_w = last_window_w_ > 0 ? last_window_w_ : cur_w;
  int prev_h = last_window_h_ > 0 ? last_window_h_ : cur_h;
  for (auto& s : stars_) {
    s.x = (s.x / static_cast<float>(prev_w)) * static_cast<float>(cur_w);
    s.y = (s.y / static_cast<float>(prev_h)) * static_cast<float>(cur_h);
  }
  last_window_w_ = cur_w;
  last_window_h_ = cur_h;
  AdjustStarDensity();
}

void StarsFields::CheckDensityChange() noexcept {
  const int cur_density = config_ ? config_->DetailsLevel() : 2;
  if (cur_density != last_density_level_) {
    last_density_level_ = cur_density;
    AdjustStarDensity();
  }
}

void StarsFields::CheckWindowResize() noexcept {
  int cur_w = Gfx::Inst().WindowWidth();
  int cur_h = Gfx::Inst().WindowHeight();
  if (last_window_w_ > 0 && last_window_h_ > 0 && (cur_w != last_window_w_ || cur_h != last_window_h_)) {
    OnResize(static_cast<float>(cur_w) / static_cast<float>(last_window_w_),
             static_cast<float>(cur_h) / static_cast<float>(last_window_h_));
  }
  last_window_w_ = cur_w;
  last_window_h_ = cur_h;
}

void StarsFields::RespawnStar(Star& s, bool random_initial_y) {
  int win_w = std::max(1, Gfx::Inst().WindowWidth());
  int win_h = std::max(1, Gfx::Inst().WindowHeight());
  s.x = rng_.UniformFloat(0.0f, static_cast<float>(win_w));
  s.y = random_initial_y ? rng_.UniformFloat(0.0f, static_cast<float>(win_h)) : -5.0f;

  float roll = rng_.UniformFloat(0.0f, 1.0f);
  if (roll < GameRules::Starfield::kForegroundLayerThreshold) {
    s.type = StarType::MidField;
    s.base_speed = rng_.UniformFloat(0.25f, 0.45f);
  } else {
    s.type = StarType::Foreground;
    s.base_speed = rng_.UniformFloat(0.45f, 0.70f);
  }
  const float phase = rng_.UniformFloat(0.0f, 6.28318530718f);
  const float speed = rng_.UniformFloat(0.03f, 0.08f);
  s.twinkle_phase = static_cast<uint16_t>(
      static_cast<uint32_t>(
          phase * static_cast<float>(GameRules::Starfield::kTwinklePhaseScale) /
          6.28318530718f));
  s.twinkle_step = static_cast<uint16_t>(std::max(
      1.0f,
      std::round(speed * static_cast<float>(GameRules::Starfield::kTwinklePhaseScale) /
                  6.28318530718f)));

  // Classificação Espectral Astrofísica OBAFGKM
  const int spec = rng_.UniformInt(0, 99);
  if (spec < 12) {
    // Tipo O/B: Gigantes Azuis Luminosas (12%)
    s.spectral = SpectralClass::Blue;
    s.r = static_cast<uint8_t>(rng_.UniformInt(170, 205));
    s.g = static_cast<uint8_t>(rng_.UniformInt(215, 240));
    s.b = 255;
  } else if (spec < 40) {
    // Tipo A/F: Estrelas Brancas e Creme (28%)
    s.spectral = SpectralClass::White;
    s.r = static_cast<uint8_t>(rng_.UniformInt(240, 255));
    s.g = static_cast<uint8_t>(rng_.UniformInt(240, 255));
    s.b = static_cast<uint8_t>(rng_.UniformInt(245, 255));
  } else if (spec < 75) {
    // Tipo G: Anãs Amarelas Solares (35%)
    s.spectral = SpectralClass::Yellow;
    s.r = 255;
    s.g = static_cast<uint8_t>(rng_.UniformInt(225, 245));
    s.b = static_cast<uint8_t>(rng_.UniformInt(160, 200));
  } else {
    // Tipo K/M: Anãs Laranjas e Vermelhas (25%)
    s.spectral = SpectralClass::OrangeRed;
    s.r = 255;
    s.g = static_cast<uint8_t>(rng_.UniformInt(140, 185));
    s.b = static_cast<uint8_t>(rng_.UniformInt(90, 140));
  }
}

void StarsFields::Scroll() {
  CheckWindowResize();
  CheckDensityChange();
  int win_h = std::max(1, Gfx::Inst().WindowHeight());
  const size_t max_active = stars_.size();

  if (warp_timer_ > 0) {
    --warp_timer_;
    float p = 1.0f - static_cast<float>(warp_timer_) / static_cast<float>(warp_duration_);
    warp_factor_ = (p < 0.65f) ? GameRules::Progression::kHyperspaceWarpSpeedMultiplier
                               : 1.0f + (GameRules::Progression::kHyperspaceWarpSpeedMultiplier - 1.0f) *
                                        (0.5f + 0.5f * std::cos((p - 0.65f) / 0.35f * 3.14159265f));
  } else if (warp_factor_ > 1.0f) {
    warp_factor_ += (1.0f - warp_factor_) * 0.05f;
    if (warp_factor_ < 1.02f) warp_factor_ = 1.0f;
  }
  for (size_t i = 0; i < max_active; ++i) {
    auto& s = stars_[i];
    s.y += s.base_speed * warp_factor_;
    s.twinkle_phase = static_cast<uint16_t>(s.twinkle_phase + s.twinkle_step);
    if (s.y >= static_cast<float>(win_h) + 15.0f) RespawnStar(s, false);
  }
}

void StarsFields::Draw() {
  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;
  if (!star_flare_tex_) InitFlareTexture(renderer);
  CheckDensityChange();

  const int density_level = config_ ? config_->DetailsLevel() : 2;
  if (density_level == 0) return;

  const size_t max_active = stars_.size();
  const bool is_warping = (warp_factor_ > 1.25f);
  const float scale_dpi = std::clamp(Gfx::Inst().Scale(), 0.9f, 1.6f);

  // Hyperspace warp: build all streaks first, then submit one geometry batch.
  // A one-pixel quad preserves the original vertical line appearance while
  // allowing each star to keep its own RGB color without per-star draw calls.
  if (is_warping) {
    static thread_local std::vector<SDL_Vertex> warp_vertices;
    static thread_local std::vector<int> warp_indices;

    constexpr float kStreakWidth = 1.0f;
    warp_vertices.clear();
    warp_indices.clear();

    warp_vertices.reserve(max_active * 4);
    warp_indices.reserve(max_active * 6);

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

      const int base = static_cast<int>(warp_vertices.size());
      warp_vertices.push_back({{left, top}, color, {0.0f, 0.0f}});
      warp_vertices.push_back({{right, top}, color, {0.0f, 0.0f}});
      warp_vertices.push_back({{right, s.y}, color, {0.0f, 0.0f}});
      warp_vertices.push_back({{left, s.y}, color, {0.0f, 0.0f}});

      warp_indices.push_back(base);
      warp_indices.push_back(base + 1);
      warp_indices.push_back(base + 2);
      warp_indices.push_back(base);
      warp_indices.push_back(base + 2);
      warp_indices.push_back(base + 3);
    }

    if (!warp_vertices.empty()) {
      SDL_RenderGeometry(renderer, nullptr, warp_vertices.data(),
                         static_cast<int>(warp_vertices.size()),
                         warp_indices.data(),
                         static_cast<int>(warp_indices.size()));
    }
    return;
  }

  // Pontos de estrelas de fundo agrupados por classe
  static thread_local std::vector<SDL_FPoint> pts_blue;
  static thread_local std::vector<SDL_FPoint> pts_white;
  static thread_local std::vector<SDL_FPoint> pts_yellow;
  static thread_local std::vector<SDL_FPoint> pts_orange;
  static thread_local std::vector<SDL_FPoint> pts_dim;
  pts_blue.clear();
  pts_white.clear();
  pts_yellow.clear();
  pts_orange.clear();
  pts_dim.clear();

  // Instâncias de flares em primeiro plano agrupadas por classe
  struct FlareInstance {
    SDL_FRect dst;
    uint8_t alpha;
  };
  static thread_local std::vector<FlareInstance> flares_blue;
  static thread_local std::vector<FlareInstance> flares_white;
  static thread_local std::vector<FlareInstance> flares_yellow;
  static thread_local std::vector<FlareInstance> flares_orange;
  flares_blue.clear();
  flares_white.clear();
  flares_yellow.clear();
  flares_orange.clear();

  for (size_t i = 0; i < max_active; ++i) {
    const auto& s = stars_[i];
    const float twinkle = FastTwinkleValue(
        static_cast<uint8_t>(s.twinkle_phase >>
                             (sizeof(s.twinkle_phase) * 8u - 8u)));

    if (s.type == StarType::MidField) {
      if (twinkle < 0.88f) {
        pts_dim.push_back({s.x, s.y});
      } else {
        switch (s.spectral) {
          case SpectralClass::Blue:      pts_blue.push_back({s.x, s.y}); break;
          case SpectralClass::White:     pts_white.push_back({s.x, s.y}); break;
          case SpectralClass::Yellow:    pts_yellow.push_back({s.x, s.y}); break;
          case SpectralClass::OrangeRed: pts_orange.push_back({s.x, s.y}); break;
        }
      }
    } else {
      const float star_size = (12.0f + 3.0f * twinkle) * scale_dpi;
      const SDL_FRect dst = {s.x - star_size * 0.5f, s.y - star_size * 0.5f, star_size, star_size};
      const uint8_t alpha = static_cast<uint8_t>(std::clamp(twinkle * 255.0f, 70.0f, 255.0f));

      switch (s.spectral) {
        case SpectralClass::Blue:      flares_blue.push_back({dst, alpha}); break;
        case SpectralClass::White:     flares_white.push_back({dst, alpha}); break;
        case SpectralClass::Yellow:    flares_yellow.push_back({dst, alpha}); break;
        case SpectralClass::OrangeRed: flares_orange.push_back({dst, alpha}); break;
      }
    }
  }

  // Desenha os pontos de estrelas médias em apenas 5 lotes
  if (!pts_blue.empty()) {
    SDL_SetRenderDrawColor(renderer, 185, 220, 255, 235);
    SDL_RenderPoints(renderer, pts_blue.data(), static_cast<int>(pts_blue.size()));
  }
  if (!pts_white.empty()) {
    SDL_SetRenderDrawColor(renderer, 245, 250, 255, 250);
    SDL_RenderPoints(renderer, pts_white.data(), static_cast<int>(pts_white.size()));
  }
  if (!pts_yellow.empty()) {
    SDL_SetRenderDrawColor(renderer, 255, 245, 190, 240);
    SDL_RenderPoints(renderer, pts_yellow.data(), static_cast<int>(pts_yellow.size()));
  }
  if (!pts_orange.empty()) {
    SDL_SetRenderDrawColor(renderer, 255, 175, 120, 225);
    SDL_RenderPoints(renderer, pts_orange.data(), static_cast<int>(pts_orange.size()));
  }
  if (!pts_dim.empty()) {
    SDL_SetRenderDrawColor(renderer, 130, 140, 170, 150);
    SDL_RenderPoints(renderer, pts_dim.data(), static_cast<int>(pts_dim.size()));
  }

  // Submit every foreground flare in one geometry batch.
  // Per-vertex color/alpha replaces texture modulation, avoiding one
  // SDL_RenderTexture call and one texture-state update for every flare.
  static thread_local std::vector<SDL_Vertex> flare_vertices;
  static thread_local std::vector<int> flare_indices;
  const size_t flare_count =
      flares_blue.size() + flares_white.size() + flares_yellow.size() + flares_orange.size();

  flare_vertices.clear();
  flare_indices.clear();
  flare_vertices.reserve(flare_count * 4);
  flare_indices.reserve(flare_count * 6);

  auto AppendFlareBatch = [&](const std::vector<FlareInstance>& flares,
                              uint8_t r, uint8_t g, uint8_t b) {
    const float rf = static_cast<float>(r) / 255.0f;
    const float gf = static_cast<float>(g) / 255.0f;
    const float bf = static_cast<float>(b) / 255.0f;

    for (const auto& item : flares) {
      const SDL_FRect& d = item.dst;
      const float af = static_cast<float>(item.alpha) / 255.0f;
      const int base = static_cast<int>(flare_vertices.size());

      const SDL_FColor color{rf, gf, bf, af};
      flare_vertices.push_back({{d.x, d.y}, color, {0.0f, 0.0f}});
      flare_vertices.push_back({{d.x + d.w, d.y}, color, {1.0f, 0.0f}});
      flare_vertices.push_back({{d.x + d.w, d.y + d.h}, color, {1.0f, 1.0f}});
      flare_vertices.push_back({{d.x, d.y + d.h}, color, {0.0f, 1.0f}});

      flare_indices.push_back(base + 0);
      flare_indices.push_back(base + 1);
      flare_indices.push_back(base + 2);
      flare_indices.push_back(base + 0);
      flare_indices.push_back(base + 2);
      flare_indices.push_back(base + 3);
    }
  };

  AppendFlareBatch(flares_blue, 185, 225, 255);
  AppendFlareBatch(flares_white, 250, 250, 255);
  AppendFlareBatch(flares_yellow, 255, 235, 180);
  AppendFlareBatch(flares_orange, 255, 160, 110);

  if (!flare_vertices.empty()) {
    SDL_RenderGeometry(renderer, star_flare_tex_, flare_vertices.data(),
                       static_cast<int>(flare_vertices.size()),
                       flare_indices.data(),
                       static_cast<int>(flare_indices.size()));
  }
}
