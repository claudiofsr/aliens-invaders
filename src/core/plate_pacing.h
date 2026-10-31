#ifndef PLATE_PACING_H
#define PLATE_PACING_H

#include "constants.h"

/** @namespace PlatePacing @brief Pure, allocation-free pacing of cached GPU plate rebakes. One
 * discipline, every plate subsystem (score HUD, telemetry overlay): the three defer clocks + ... */
namespace PlatePacing {

/// Per-subsystem defer clocks behind the settling/calm/warm gates. Caps and predicates stay in
/// GameRules::Visuals (SSOT); this class only sequences them, so both plate subsystems share ...
class Scheduler {
  int settling_defer_frames_{0};
  int calm_defer_frames_{0};
  int warm_defer_frames_{0};

 public:
  constexpr Scheduler() noexcept = default;

  /// Counts one frame of settling/calm deferral for a wanted rebake; each
  /// clock resets the moment its pressure ends (pure policies, SSOT caps).
  constexpr void AdvanceRebakeClocks(bool settling_wanted, bool calm_wanted,
                                     bool text_settling,
                                     bool present_calm) noexcept {
    settling_defer_frames_ = GameRules::Visuals::NextSettlingDeferFrames(
        settling_defer_frames_, settling_wanted, text_settling);
    calm_defer_frames_ = GameRules::Visuals::NextCalmDeferFrames(
        calm_defer_frames_, calm_wanted, present_calm);
  }

  /// Counts one frame of an incomplete budgeted warm pass. Pass false when no slice ran (or it
  /// finished): the clock parks at zero, so every warm window starts with the full budget.
  constexpr void AdvanceWarmClock(bool warm_incomplete) noexcept {
    warm_defer_frames_ = GameRules::Visuals::NextPlateWarmDeferFrames(
        warm_defer_frames_, warm_incomplete);
  }

  /// Settling gate: warm glyphs release the wait early; otherwise bounded by
  /// kPlateSettlingMaxDeferFrames.
  [[nodiscard]] constexpr bool SettlingOk(bool text_settling,
                                          bool warm_complete) const noexcept {
    return warm_complete ||
           GameRules::Visuals::SettlingRebakeMayProceed(
               text_settling, settling_defer_frames_,
               GameRules::Visuals::kPlateSettlingMaxDeferFrames);
  }

  /// Calm gate: the target switch waits for a quiet swapchain, bounded by
  /// kCalmRebakeMaxDeferFrames (correctness-first callers OR their bypass).
  [[nodiscard]] constexpr bool CalmOk(bool present_calm) const noexcept {
    return GameRules::Visuals::CalmRebakeMayProceed(
        present_calm, calm_defer_frames_,
        GameRules::Visuals::kCalmRebakeMaxDeferFrames);
  }

  /// Warm gate: a COLD bake is forced only after kPlateWarmMaxDeferFrames.
  [[nodiscard]] constexpr bool WarmOk(bool warm_complete) const noexcept {
    return warm_complete ||
           GameRules::Visuals::PlateWarmBakeMayProceed(warm_defer_frames_);
  }

  /// A landed rebake resets every clock: fresh budget for the next window.
  constexpr void OnRebakeLanded() noexcept {
    settling_defer_frames_ = 0;
    calm_defer_frames_ = 0;
    warm_defer_frames_ = 0;
  }
};

}  // namespace PlatePacing

#endif  // PLATE_PACING_H
