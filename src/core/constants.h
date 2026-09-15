#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <cmath>
#include "game_rules.h"
#include "gfxinterface.h"

namespace FleetMetrics {
  [[nodiscard]] inline int AlienWidth() noexcept {
    return static_cast<int>(std::round(GameRules::Fleet::kAlienBaseWidthPixels * Gfx::Inst().Scale()));
  }
  [[nodiscard]] inline int AlienHeight() noexcept {
    return static_cast<int>(std::round(GameRules::Fleet::kAlienBaseHeightPixels * Gfx::Inst().Scale()));
  }
  [[nodiscard]] inline int AlienHSpacing() noexcept {
    return static_cast<int>(std::round(GameRules::Fleet::kAlienHorizontalSpacingPixels * Gfx::Inst().Scale()));
  }
  [[nodiscard]] inline int AlienVSpacing() noexcept {
    return static_cast<int>(std::round(GameRules::Fleet::kAlienVerticalSpacingPixels * Gfx::Inst().Scale()));
  }
  [[nodiscard]] inline int BaseCruiseY() noexcept {
    return static_cast<int>(std::round(GameRules::Fleet::kFleetBaseCruiseYPixels * Gfx::Inst().Scale()));
  }
}

[[nodiscard]] inline int AlienWidth() noexcept { return FleetMetrics::AlienWidth(); }
[[nodiscard]] inline int AlienHeight() noexcept { return FleetMetrics::AlienHeight(); }
[[nodiscard]] inline int AlienHSpacing() noexcept { return FleetMetrics::AlienHSpacing(); }
[[nodiscard]] inline int AlienVSpacing() noexcept { return FleetMetrics::AlienVSpacing(); }
[[nodiscard]] inline int BaseCruiseY() noexcept { return FleetMetrics::BaseCruiseY(); }

#endif  // CONSTANTS_H
