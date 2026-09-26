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
// Precalculated uint8_t brightness table [0..255]: eliminates roundf32 and float math in hot loop
[[nodiscard]] inline uint8_t FastTwinkleAlpha(uint8_t index) noexcept {
  static const auto s_table = []() {
    std::array<uint8_t, GameRules::Starfield::kTwinkleTableSize> table{};
    for (size_t i = 0; i < table.size(); ++i) {
      const float angle = static_cast<float>(i) *
                          (GameRules::Starfield::kTau /
                           static_cast<float>(GameRules::Starfield::kTwinkleTableSize));
      // Atmospheric scintillation, built ONCE at static-init.
      // Primary sine + 2nd/3rd harmonics (seeing ripples), then n*n so the
      // star spends more time dim and flashes at the crest — a real twinkle,
      // not a slow 15 % fade. Hot loop remains a single uint8_t LUT lookup.
      const float s1 = std::sin(angle);
      const float s2 = std::sin(2.0f * angle + 0.9f);
      const float s3 = std::sin(3.0f * angle + 2.1f);
      float n = 0.5f + 0.5f * s1
              + GameRules::Starfield::kTwinkleHarmonic2 * s2
              + GameRules::Starfield::kTwinkleHarmonic3 * s3;
      n = std::clamp(n, 0.0f, 1.0f);
      n *= n;
      const float val =
          (GameRules::Starfield::kTwinkleFloor +
           (GameRules::Starfield::kTwinkleCeil - GameRules::Starfield::kTwinkleFloor) * n) *
          255.0f;
      table[i] = static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f));
    }
    return table;
  }();
  return s_table[index];
}

// Precalculated scintillation pulse factor [0.0f..1.0f]: eliminates float division in hot loop
[[nodiscard]] inline float FastTwinklePulse(uint8_t index) noexcept {
  static const auto s_pulse_table = []() {
    std::array<float, GameRules::Starfield::kTwinkleTableSize> table{};
    for (size_t i = 0; i < table.size(); ++i) {
      const float tw = static_cast<float>(FastTwinkleAlpha(static_cast<uint8_t>(i))) * (1.0f / 255.0f);
      table[i] = GameRules::Starfield::Pulse01(tw);
    }
    return table;
  }();
  return s_pulse_table[index];
}
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

StarsFields::StarsFields() {
  // Fixed std::array pool - capacity is compile-time (kStarPoolCapacity).
  last_window_w_ = std::max(1280, Gfx::Inst().WindowWidth());
  last_window_h_ = std::max(720, Gfx::Inst().WindowHeight());
  AdjustStarDensity();
  for (size_t i = 0; i < active_stars_; ++i) RespawnStar(stars_[i], true);
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
  // Clamp to the fixed pool capacity (compile-time ceiling). Never allocates.
  const size_t target = std::min(CalculateTargetStarCount(), kStarPoolCapacity);
  if (target > active_stars_) {
    for (size_t i = active_stars_; i < target; ++i) RespawnStar(stars_[i], true);
  }
  active_stars_ = target;
}

void StarsFields::CheckDensityChange() {
  const int cur_density = config_ ? config_->DetailsLevel() : 2;
  if (cur_density != last_density_level_) {
    last_density_level_ = cur_density;
    AdjustStarDensity();
  }
}

void StarsFields::CheckWindowResize() {
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

void StarsFields::OnResize(float rx, float ry) {
  if (rx <= 0.0f || ry <= 0.0f) return;
  for (size_t i = 0; i < active_stars_; ++i) {
    auto& s = stars_[i];
    s.x *= rx;
    s.y *= ry;
  }
  last_window_w_ = Gfx::Inst().WindowWidth();
  last_window_h_ = Gfx::Inst().WindowHeight();
  AdjustStarDensity();
}

void StarsFields::RespawnStar(Star& s, bool random_initial_y) {
  int win_w = std::max(1, Gfx::Inst().WindowWidth());
  int win_h = std::max(1, Gfx::Inst().WindowHeight());
  s.x = rng_.UniformFloat(0.0f, static_cast<float>(win_w));
  s.y = random_initial_y ? rng_.UniformFloat(0.0f, static_cast<float>(win_h)) : -5.0f;

  float roll = rng_.UniformFloat(0.0f, 1.0f);
  if (roll < GameRules::Starfield::kForegroundLayerThreshold) {
    s.type = StarType::MidField;
    s.base_speed = rng_.UniformFloat(GameRules::Starfield::kMidFieldFallSpeedMinPx,
                                     GameRules::Starfield::kMidFieldFallSpeedMaxPx);
  } else {
    s.type = StarType::Foreground;
    s.base_speed = rng_.UniformFloat(GameRules::Starfield::kForegroundFallSpeedMinPx,
                                     GameRules::Starfield::kForegroundFallSpeedMaxPx);
  }
  const float phase = rng_.UniformFloat(0.0f, GameRules::Starfield::kTau);
  const float speed = rng_.UniformFloat(GameRules::Starfield::kTwinkleSpeedMin,
                                       GameRules::Starfield::kTwinkleSpeedMax);
  s.twinkle_phase = static_cast<uint16_t>(
      static_cast<uint32_t>(
          phase * static_cast<float>(GameRules::Starfield::kTwinklePhaseScale) /
          GameRules::Starfield::kTau));
  s.twinkle_step = static_cast<uint16_t>(std::max(
      1.0f,
      std::round(speed * static_cast<float>(GameRules::Starfield::kTwinklePhaseScale) /
                  GameRules::Starfield::kTau)));

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
  const size_t max_active = active_stars_;

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
s.twinkle_alpha = FastTwinkleAlpha(static_cast<uint8_t>(s.twinkle_phase >> 8u));
    if (s.y >= static_cast<float>(win_h) + 15.0f) RespawnStar(s, false);
  }
}

void StarsFields::Draw() {
  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;
  if (!star_flare_tex_) InitFlareTexture(renderer);
  // CheckDensityChange() já roda em Scroll(), que sempre precede Draw().

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

    // MidField stars: small textured quads (not single pixels) with
    // per-star twinkle modulating alpha (shimmer) AND half-size
    // (pulsating breath).  One SDL_RenderGeometry call for ALL
    // background stars using the flare texture (core + halo + spikes).
    static constexpr float kMidColors[4][3] = {
        {185.0f / 255.0f, 220.0f / 255.0f, 255.0f / 255.0f},  // Blue
        {245.0f / 255.0f, 250.0f / 255.0f, 255.0f / 255.0f},  // White
        {255.0f / 255.0f, 245.0f / 255.0f, 190.0f / 255.0f},  // Yellow
        {255.0f / 255.0f, 175.0f / 255.0f, 120.0f / 255.0f},  // OrangeRed
    };
    // Compile-time ceiling: kStarPoolCapacity * 4 vertices. Zero heap in Draw().
    static std::array<SDL_Vertex, StarsFields::kStarPoolCapacity * 4> mid_verts{};
    size_t mid_quad_count = 0;



  // Pre-sized vertex array eliminating push_back overhead in the draw loop
  // Compile-time ceiling: kStarPoolCapacity * 4 vertices. Zero heap in Draw().
  static std::array<SDL_Vertex, StarsFields::kStarPoolCapacity * 4> flare_vertices{};
  size_t flare_quad_count = 0;

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
    const float pulse = FastTwinklePulse(phase_idx);

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
      const size_t bv = mid_quad_count * 4;
      mid_verts[bv + 0] = {{s.x - hs, s.y - hs}, color, {0.0f, 0.0f}};
      mid_verts[bv + 1] = {{s.x + hs, s.y - hs}, color, {1.0f, 0.0f}};
      mid_verts[bv + 2] = {{s.x + hs, s.y + hs}, color, {1.0f, 1.0f}};
      mid_verts[bv + 3] = {{s.x - hs, s.y + hs}, color, {0.0f, 1.0f}};
      ++mid_quad_count;
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

      const size_t v_idx = flare_quad_count * 4;
      flare_vertices[v_idx + 0] = {{left, top}, color, {0.0f, 0.0f}};
      flare_vertices[v_idx + 1] = {{left + star_size, top}, color, {1.0f, 0.0f}};
      flare_vertices[v_idx + 2] = {{left + star_size, top + star_size}, color, {1.0f, 1.0f}};
      flare_vertices[v_idx + 3] = {{left, top + star_size}, color, {0.0f, 1.0f}};

      ++flare_quad_count;
    }
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  // Draw background star points in compact spectral batches
    // Single geometry batch for ALL MidField stars: 1 draw call,
    // star_flare_tex_ (core + halo + spikes) with bilinear filtering.
    // Reuses the compile-time s_quad_indices table (0,1,2,0,2,3 per quad).
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if (mid_quad_count > 0) {
        SDL_RenderGeometry(renderer, star_flare_tex_, mid_verts.data(),
                           static_cast<int>(mid_quad_count * 4),
                           s_quad_indices.data(),
                           static_cast<int>(mid_quad_count * 6));
    }

  if (flare_quad_count > 0) {
    SDL_RenderGeometry(renderer, star_flare_tex_, flare_vertices.data(),
                       static_cast<int>(flare_quad_count * 4),
                       s_quad_indices.data(),
                       static_cast<int>(flare_quad_count * 6));
  }
}
