#ifndef CONFIG_H
#define CONFIG_H

#include <string>

#include "embedded_assets.h"

/**
 * @class Config
 * @brief Thread-safe configuration manager for player preferences and universal display pacing.
 */
class Config {
  static constexpr int min_details = 0;
  static constexpr int max_details = 4;
  int details_level_{2};
  int refresh_rate_{60};
  bool use_alt_ship_{false};
  std::string player_name_;
  std::string score_file_name_;

  Config();

 public:
  static Config& Instance() {
    static Config instance;
    return instance;
  }

  Config(const Config&) = delete;
  Config& operator=(const Config&) = delete;

  void AddDetailsLevel(int i) noexcept;
  [[nodiscard]] int ScaleDetails(int val) const noexcept {
    return val * details_level_ / max_details;
  }
  [[nodiscard]] int DetailsLevel() const noexcept { return details_level_; }
  [[nodiscard]] int MaxDetails() const noexcept { return max_details; }

  [[nodiscard]] int RefreshRate() const noexcept { return refresh_rate_; }
  // Supports any modern display frequency: 24 Hz, 30 Hz, 60 Hz up to 600 Hz and 1000 Hz
  void SetRefreshRate(int rate) noexcept {
    if (rate >= 24 && rate <= 1000) refresh_rate_ = rate;
  }

  [[nodiscard]] bool UseAltShip() const noexcept { return use_alt_ship_; }
  void ToggleShipModel() noexcept { use_alt_ship_ = !use_alt_ship_; }
  [[nodiscard]] SpriteId PlayerSpriteId() const noexcept {
    return use_alt_ship_ ? SpriteId::PlayerAlt : SpriteId::Player;
  }

  [[nodiscard]] const std::string& GetPlayerName() const noexcept {
    return player_name_;
  }
  [[nodiscard]] const std::string& GetScoreFileName() const noexcept {
    return score_file_name_;
  }
};

void SetStandardWindowSize(unsigned win_size);

#endif  // CONFIG_H
