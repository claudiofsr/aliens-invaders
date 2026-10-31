#ifndef TIME_UTIL_H
#define TIME_UTIL_H

#include <cstdint>

#include "constants.h"

/// Monotonic wall-clock time in seconds. Never use this value for game state.
[[nodiscard]] double CurrentMicroSecond() noexcept;

/// Pace a non-vsync render loop without an unbounded busy-wait.
[[nodiscard]] double FramePacer(double frame_start_time,
                                double target_interval,
                                bool vsync_active) noexcept;

/** @brief Fixed-step simulation clock with bounded catch-up. The simulation is intentionally
 * independent of the display refresh rate. A long OS scheduling pause is treated as lost real ... */
class FramePacingEngine final {
 public:
  explicit FramePacingEngine(int refresh_rate = 60,
                             bool vsync = true) noexcept;

  void Configure(int refresh_rate, bool vsync) noexcept;
  void Reset() noexcept;

  /// Starts a render frame and accumulates a bounded amount of simulation time.
  [[nodiscard]] double BeginFrame() noexcept;

  /// Consumes at most one fixed simulation step.
  [[nodiscard]] bool ShouldStepPhysics() noexcept;

  /// Fraction of the next fixed step left for render interpolation.
  [[nodiscard]] float InterpolationAlpha() const noexcept;

  /// Sleeps only when software frame pacing is active.
  void EndFrame(double frame_start_time) noexcept;

 private:
  // SSOT: every tunable lives in GameRules::Simulation and the stall policy in the pure Pacing::
  // functions (frame_pacing.h). This class only owns the mutable state and the clock reads, so ...
  std::uint64_t previous_ns_{0};
  std::uint64_t accumulator_ns_{0};
  std::uint64_t target_frame_ns_{GameRules::Simulation::kFixedStepNanoseconds};
  bool vsync_active_{true};

  int steps_this_frame_{0};
};

#endif  // TIME_UTIL_H
