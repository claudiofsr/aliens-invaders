#include "stars.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "config.h"
#include "constants.h"
#include "math_types.h"

namespace {
// Precalculated uint8_t brightness table [0..255]: eliminates roundf32 and float math in hot loop
[[nodiscard]] inline uint8_t FastTwinkleAlpha(uint8_t index) noexcept {
  static const auto s_table = []() {
    std::array<uint8_t, GameRules::Starfield::kTwinkleTableSize> table{};
    for (size_t i = 0; i < table.size(); ++i) {
      const float angle = static_cast<float>(i) *
                          (GameRules::Starfield::kTau /
                           static_cast<float>(GameRules::Starfield::kTwinkleTableSize));
      // Atmospheric scintillation, built ONCE at static-init. Primary sine + 2nd/3rd harmonics
      // (seeing ripples), then n*n so the star spends more time dim and flashes at the crest — ...
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
}  // namespace

uint8_t StarsFields::TwinkleAlpha(uint8_t index) noexcept { return FastTwinkleAlpha(index); }

float StarsFields::TwinklePulse(uint8_t index) noexcept { return FastTwinklePulse(index); }

StarsFields::StarsFields(int viewport_w, int viewport_h) {
  // Fixed std::array pool - capacity is compile-time (kStarPoolCapacity). The tracked size must
  // mirror the LIVE window the stars are spawned in (guarded only against a zero divisor).
  // Headless consumers (dump_vectors, parity tests) feed the viewport directly; the production
  // default constructor in stars_draw.cc feeds the live Gfx singleton.
  last_window_w_ = std::max(1, viewport_w);
  last_window_h_ = std::max(1, viewport_h);
  AdjustStarDensity();
  for (size_t i = 0; i < active_stars_; ++i) RespawnStar(stars_[i], true);
}

size_t StarsFields::CalculateTargetStarCount() const noexcept {
  const int density_level = config_ ? config_->DetailsLevel() : 2;
  if (density_level == 0) return 0;

  const int w = std::max(640, last_window_w_);
  const int h = std::max(480, last_window_h_);
  const double kBaseArea = static_cast<double>(GameRules::Starfield::kBaseResolutionWidthPixels) *
                           static_cast<double>(GameRules::Starfield::kBaseResolutionHeightPixels);
  const double current_area = static_cast<double>(w) * static_cast<double>(h);
  const double scale = std::sqrt(current_area / kBaseArea);
  size_t base_target = static_cast<size_t>(FastRound(static_cast<double>(GameRules::Starfield::kBaseStarCount) * scale));

  // Calibrated canonical density factor taken straight from the SSOT in constants.h
  const double factor = GameRules::Starfield::GetDensityFactor(density_level);

  size_t target = static_cast<size_t>(FastRound(static_cast<double>(base_target) * factor));
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

void StarsFields::SetViewport(int viewport_w, int viewport_h) {
  const int w = std::max(1, viewport_w);
  const int h = std::max(1, viewport_h);
  if (w == last_window_w_ && h == last_window_h_) return;
  const float rx = static_cast<float>(w) / static_cast<float>(last_window_w_);
  const float ry = static_cast<float>(h) / static_cast<float>(last_window_h_);
  last_window_w_ = w;
  last_window_h_ = h;
  OnResize(rx, ry);
}

void StarsFields::OnResize(float rx, float ry) {
  if (rx <= 0.0f || ry <= 0.0f) return;
  for (size_t i = 0; i < active_stars_; ++i) {
    auto& s = stars_[i];
    s.x *= rx;
    s.y *= ry;
  }
  // The tracked viewport mirror is owned by the callers (CheckWindowResize in
  // stars_draw.cc, SetViewport below): both update it BEFORE calling in, so
  // the density recalculation reads the new window.
  AdjustStarDensity();
}

void StarsFields::RespawnStar(Star& s, bool random_initial_y) {
  const int win_w = std::max(1, last_window_w_);
  const int win_h = std::max(1, last_window_h_);
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
      1,
      FastRound(speed * static_cast<float>(GameRules::Starfield::kTwinklePhaseScale) /
                  GameRules::Starfield::kTau)));

  // OBAFGKM astrophysical spectral classification
  const int spec = rng_.UniformInt(0, 99);
  if (spec < 12) {
    // Type O/B: luminous blue giants (12%)
    s.spectral = SpectralClass::Blue;
    s.r = static_cast<uint8_t>(rng_.UniformInt(170, 205));
    s.g = static_cast<uint8_t>(rng_.UniformInt(215, 240));
    s.b = 255;
  } else if (spec < 40) {
    // Type A/F: white and cream stars (28%)
    s.spectral = SpectralClass::White;
    s.r = static_cast<uint8_t>(rng_.UniformInt(240, 255));
    s.g = static_cast<uint8_t>(rng_.UniformInt(240, 255));
    s.b = static_cast<uint8_t>(rng_.UniformInt(245, 255));
  } else if (spec < 75) {
    // Type G: solar-type yellow dwarfs (35%)
    s.spectral = SpectralClass::Yellow;
    s.r = 255;
    s.g = static_cast<uint8_t>(rng_.UniformInt(225, 245));
    s.b = static_cast<uint8_t>(rng_.UniformInt(160, 200));
  } else {
    // Type K/M: orange and red dwarfs (25%)
    s.spectral = SpectralClass::OrangeRed;
    s.r = 255;
    s.g = static_cast<uint8_t>(rng_.UniformInt(140, 185));
    s.b = static_cast<uint8_t>(rng_.UniformInt(90, 140));
  }
}

void StarsFields::Scroll() {
  // Production call sites run CheckWindowResize() (shell side, stars_draw.cc)
  // immediately before Scroll(); everything below reads the tracked mirror.
  CheckDensityChange();
  const int win_h = std::max(1, last_window_h_);
  const size_t max_active = active_stars_;

  if (warp_timer_ > 0) {
    --warp_timer_;
    float p = 1.0f - static_cast<float>(warp_timer_) / static_cast<float>(warp_duration_);
    warp_factor_ = (p < 0.65f) ? GameRules::Progression::kHyperspaceWarpSpeedMultiplier
                               : 1.0f + (GameRules::Progression::kHyperspaceWarpSpeedMultiplier - 1.0f) *
                                        (0.5f + 0.5f * std::cos((p - 0.65f) / 0.35f * Pi));
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


StarsFields::StarSample StarsFields::SampleStar(size_t i) const noexcept {
  const Star& s = stars_[i];
  return StarSample{s.x,
                    s.y,
                    s.base_speed,
                    static_cast<uint8_t>(s.type),
                    static_cast<uint8_t>(s.spectral),
                    s.r,
                    s.g,
                    s.b,
                    s.twinkle_alpha,
                    s.twinkle_phase,
                    s.twinkle_step};
}
