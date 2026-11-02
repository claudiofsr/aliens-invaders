#ifndef STARS_H
#define STARS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "constants.h"
#include "math_types.h"
#include "random_stream.h"

class Config;

struct SDL_Texture;
struct SDL_Renderer;

class StarsFields {
 public:
  enum class StarType : uint8_t {
    MidField = 0,
    Foreground = 1
  };

  enum class SpectralClass : uint8_t {
    Blue = 0,
    White = 1,
    Yellow = 2,
    OrangeRed = 3
  };

 private:
  struct Star {
    float x{0.0f};
    float y{0.0f};
    float base_speed{1.0f};
    StarType type{StarType::MidField};
    SpectralClass spectral{SpectralClass::White};
    uint8_t r{255}, g{255}, b{255};
    uint16_t twinkle_phase{0};
    uint16_t twinkle_step{1};
    uint8_t twinkle_alpha{255};  // precomputed brightness from Scroll()
  };

  // Fixed-capacity star pool. Zero realloc on density / resolution changes.
  static constexpr std::size_t kStarPoolCapacity =
      GameRules::Starfield::kMaxStarsCount * 2;
  std::array<Star, kStarPoolCapacity> stars_{};
  std::size_t active_stars_{0};
  float warp_factor_{1.0f};
  int warp_timer_{0};
  int warp_duration_{0};
  RandomStream rng_{0xC0531CC1u};
  const Config* config_{nullptr};

  int last_window_w_{0};
  int last_window_h_{0};
  int last_density_level_{2};

  SDL_Texture* star_flare_tex_{nullptr};
  // Flare-release hook: the graphics shell binds SDL_DestroyTexture at
  // startup (binder in stars_draw.cc); headless builds leave it null (a
  // headless starfield owns no GPU texture). Same contract as Pix.
  inline static void (*flare_deleter_)(SDL_Texture*){nullptr};

 public:
  StarsFields();
  /// Headless constructor (golden-vector tooling): the tracked viewport comes
  /// from the caller instead of the live Gfx singleton. The production default
  /// constructor (stars_draw.cc) delegates with the live window size.
  StarsFields(int viewport_w, int viewport_h);
  ~StarsFields() {
    if (star_flare_tex_ && flare_deleter_) flare_deleter_(star_flare_tex_);
  }

  StarsFields(const StarsFields&) = delete;
  StarsFields& operator=(const StarsFields&) = delete;

  void TriggerHyperspaceWarp(int duration_frames = 135) noexcept {
    warp_duration_ = duration_frames;
    warp_timer_ = duration_frames;
    warp_factor_ = GameRules::Progression::kHyperspaceWarpSpeedMultiplier;
  }
  void SetConfig(const Config* cfg) { config_ = cfg; AdjustStarDensity(); }

  /// Explicit viewport change (headless twin of CheckWindowResize): scales the
  /// pool by the size ratio, updates the tracked mirror and re-fits density.
  void SetViewport(int viewport_w, int viewport_h);

  void OnResize(float rx, float ry);
  void CheckWindowResize();
  void CheckDensityChange();

  void Scroll();
  void Draw();

  /// One-star observation sample for the golden-vector tooling
  /// (tools/dump_vectors.cc and the Rust parity test).
  struct StarSample {
    float x;
    float y;
    float base_speed;
    uint8_t type;
    uint8_t spectral;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t alpha;
    uint16_t twinkle_phase;
    uint16_t twinkle_step;
  };
  [[nodiscard]] size_t ActiveStars() const noexcept { return active_stars_; }
  [[nodiscard]] StarSample SampleStar(size_t i) const noexcept;
  [[nodiscard]] int WarpTimer() const noexcept { return warp_timer_; }
  [[nodiscard]] float WarpFactor() const noexcept { return warp_factor_; }
  [[nodiscard]] int ViewportWidth() const noexcept { return last_window_w_; }
  [[nodiscard]] int ViewportHeight() const noexcept { return last_window_h_; }

  /// Frozen-lookup observation of the twinkle LUTs: their sine content is
  /// transcendental, so golden vectors pin the RECORDED bits (the Rust port
  /// embeds the recorded tables instead of recomputing them).
  static void SetFlareDeleter(void (*fn)(SDL_Texture*)) noexcept {
    flare_deleter_ = fn;
  }
  [[nodiscard]] static uint8_t TwinkleAlpha(uint8_t index) noexcept;
  [[nodiscard]] static float TwinklePulse(uint8_t index) noexcept;

 private:
  void RespawnStar(Star& s, bool random_initial_y);
  [[nodiscard]] size_t CalculateTargetStarCount() const noexcept;
  void AdjustStarDensity();
  void InitFlareTexture(SDL_Renderer* renderer);
};

#endif  // STARS_H
