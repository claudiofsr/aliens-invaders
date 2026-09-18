#ifndef STARS_H
#define STARS_H

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

  enum class SectorProfile : uint8_t {
    CauchyFilament = 0,
    SpiralArms = 1,
    LaplaceDisk = 2
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
  };

  std::vector<Star> stars_;
  float warp_factor_{1.0f};
  int warp_timer_{0};
  int warp_duration_{0};
  SectorProfile current_profile_{SectorProfile::CauchyFilament};
  RandomStream rng_{0xC0531CC1u};
  const Config* config_{nullptr};

  int last_window_w_{0};
  int last_window_h_{0};
  int last_density_level_{2};

  SDL_Texture* star_flare_tex_{nullptr};

 public:
  StarsFields();
  ~StarsFields();

  StarsFields(const StarsFields&) = delete;
  StarsFields& operator=(const StarsFields&) = delete;

  void SetWarpFactor(float factor) noexcept { warp_factor_ = factor; }
  void TriggerHyperspaceWarp(int duration_frames = 135) noexcept {
    warp_duration_ = duration_frames;
    warp_timer_ = duration_frames;
    warp_factor_ = GameRules::Progression::kHyperspaceWarpSpeedMultiplier;
  }
  [[nodiscard]] float GetWarpFactor() const noexcept { return warp_factor_; }

  void SetSectorProfile(SectorProfile profile) noexcept {
    current_profile_ = profile;
  }
  void SelectDistributionForLevel(int level) noexcept;
  void SetConfig(const Config* cfg) noexcept { config_ = cfg; AdjustStarDensity(); }

  void OnResize(float rx, float ry) noexcept;
  void CheckWindowResize() noexcept;
  void CheckDensityChange() noexcept;

  void Scroll();
  void Draw();

 private:
  void RespawnStar(Star& s, bool random_initial_y);
  [[nodiscard]] size_t CalculateTargetStarCount() const noexcept;
  void AdjustStarDensity();
  void InitFlareTexture(SDL_Renderer* renderer);
};

#endif  // STARS_H
