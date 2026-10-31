#ifndef PREWARM_PLAN_H
#define PREWARM_PLAN_H

// constants.h: the telemetry base is tied by static_assert to GameRules::TelemetryConfig::kFontSize
// (single source of truth), and its clamped scale band lives in the same SSOT namespace.
#include "constants.h"

/** @namespace FontPrewarm @brief Pure constexpr plan of the DPI-scale font re-rasterization (AGENT
 * 4/7). Why (anti-stuttering): - When the window scale changes (fullscreen toggle, resolution ... */
namespace FontPrewarm {

/// Combat floating-text bases: drawn at fixed design sizes, never multiplied
/// by the window scale.
inline constexpr float kCombatBases[] = {22.0f, 24.0f, 25.0f, 35.0f, 36.0f, 42.0f};
/// Menu / HUD / OSD bases: multiplied by Gfx::Scale() before size quantization -- EXCEPT the
/// telemetry base, which rides the CLAMPED osd scale (see Item::clamped_scale and ...
inline constexpr float kMenuBases[] = {14.0f, 16.0f, 18.0f, 20.0f, 22.0f,
                                       23.0f, 24.0f, 28.0f, 32.0f, 36.0f,
                                       48.0f};

inline constexpr int kCombatBaseCount = 6;
inline constexpr int kMenuBaseCount = 11;
inline constexpr int kBaseCount = kCombatBaseCount + kMenuBaseCount;

/// Design-base index for the telemetry OSD. Prewarm the clamped font size
/// actually used by the overlay.
inline constexpr int kTelemetryMenuBase = 5;
static_assert(kMenuBases[kTelemetryMenuBase] == GameRules::TelemetryConfig::kFontSize,
              "the telemetry plan base must track TelemetryConfig::kFontSize");
inline constexpr int kFirstCharCode = 32;   // ' '
inline constexpr int kLastCharCode = 126;   // '~'
inline constexpr int kCharsPerBase = kLastCharCode - kFirstCharCode + 1;  // 95
inline constexpr int kStyleCount = 2;       // regular + bold
inline constexpr int kItemCount = kBaseCount * kCharsPerBase * kStyleCount;

/// Visit order for design bases. Warm the HUD banner and telemetry OSD first, then the remaining
/// scaled menu bases and the unscaled combat bases. the plan on purpose: their pixel sizes do ...
inline constexpr int kBaseVisitOrder[kBaseCount] = {
    9,                                       // menu 20*s   -- the HUD plate
    11,                                      // menu 23*s'  -- the telemetry OSD
    6, 7, 8, 10, 12, 13, 14, 15, 16,         // remaining scaled menu bases
    0, 1, 2, 3, 4, 5                         // unscaled combat bases
};

/// Compile-time permutation contract: every logical base is visited exactly
/// once -- a duplicated or missing slot would silently skip glyphs forever.
[[nodiscard]] constexpr bool BaseVisitOrderIsPermutation() noexcept {
  for (int logical = 0; logical < kBaseCount; ++logical) {
    int seen = 0;
    for (int slot = 0; slot < kBaseCount; ++slot) {
      if (kBaseVisitOrder[slot] == logical) ++seen;
    }
    if (seen != 1) return false;
  }
  return true;
}
static_assert(BaseVisitOrderIsPermutation(),
              "kBaseVisitOrder must be a permutation of 0..kBaseCount-1");

/// One unit of work: (design base, ASCII char, style, scale flags).
struct Item {
  float base{0.0f};
  char ch{' '};
  bool bold{false};
  bool scale_by_dpi{false};
  /// True only for the telemetry OSD base: its scaled size uses the CLAMPED osd scale
  /// (GameRules::TelemetryConfig band), mirroring Telemetry::UpdateAndDraw exactly -- see ...
  bool clamped_scale{false};
};

/// Deterministic flattening addressable by a single resumable cursor: the style is the fastest
/// axis, then the char, then the base through the kBaseVisitOrder slot map (scaled HUD base ...
[[nodiscard]] constexpr Item ItemAt(int index) noexcept {
  const int clamped = (index < 0) ? 0 : ((index >= kItemCount) ? (kItemCount - 1) : index);
  const int style = clamped % kStyleCount;
  const int char_index = (clamped / kStyleCount) % kCharsPerBase;
  const int base_slot = clamped / (kStyleCount * kCharsPerBase);
  // Plan slot -> logical base through the visit order (HUD size first).
  const int base_index = kBaseVisitOrder[base_slot];
  const bool combat = (base_index < kCombatBaseCount);
  // Provably-in-bounds selection: GCC's -Warray-bounds cannot narrow base_index through the
  // ternary's condition (field build: "array subscript 9 is above array bounds of 'const float ...
  const int combat_index =
      (base_index >= 0 && base_index < kCombatBaseCount) ? base_index : 0;
  const int menu_index =
      (base_index >= kCombatBaseCount && base_index < kBaseCount)
          ? (base_index - kCombatBaseCount)
          : 0;
  Item item;
  item.base = combat ? kCombatBases[combat_index] : kMenuBases[menu_index];
  item.ch = static_cast<char>(kFirstCharCode + char_index);
  item.bold = (style == 1);
  item.scale_by_dpi = !combat;
  item.clamped_scale = !combat && (menu_index == kTelemetryMenuBase);
  return item;
}

}  // namespace FontPrewarm

#endif  // PREWARM_PLAN_H
