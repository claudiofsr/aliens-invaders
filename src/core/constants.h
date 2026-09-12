#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <cmath>

#include "gfxinterface.h"

// Alien vessel geometry (84x84 px at 1080p base scale)
inline int AlienWidth() noexcept {
  return static_cast<int>(std::round(84.0f * Gfx::Inst().Scale()));
}
inline int AlienHeight() noexcept {
  return static_cast<int>(std::round(84.0f * Gfx::Inst().Scale()));
}

// Optimal compact military formation spacing (tight visual alignment without
// overlapping wings) Horizontal: 93px (84px ship + 9px aerodynamic wingtip
// clearance)
inline int AlienHSpacing() noexcept {
  return static_cast<int>(std::round(93.0f * Gfx::Inst().Scale()));
}

// Vertical: 95px (84px ship + 11px row clearance)
inline int AlienVSpacing() noexcept {
  return static_cast<int>(std::round(95.0f * Gfx::Inst().Scale()));
}

// Top vertical fleet margin positioned right below HUD telemetry
inline int BaseCruiseY() noexcept {
  return static_cast<int>(std::round(82.0f * Gfx::Inst().Scale()));
}

#define g_alien_width (AlienWidth())
#define g_alien_height (AlienHeight())
#define g_aliens_hspacing (AlienHSpacing())
#define g_aliens_vspacing (AlienVSpacing())

#endif  // CONSTANTS_H
