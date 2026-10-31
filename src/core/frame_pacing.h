#ifndef FRAME_PACING_H
#define FRAME_PACING_H

#include <algorithm>
#include <cstdint>

#include "constants.h"

/** @namespace Pacing @brief Pure, clock-free arithmetic of the fixed-step simulation clock. Why a
 * separate header (AGENT 6 / AGENT 7): - Every function is a constexpr pure function of its ... */
namespace Pacing {

/// True when a frame took so long that the cause is almost certainly EXTERNAL to the game
/// (compositor busy, GPU/driver wait, CPU starvation, suspended process) rather than a slow ...
[[nodiscard]] constexpr bool IsHitch(std::uint64_t raw_delta_ns) noexcept {
  return raw_delta_ns >= GameRules::Simulation::kHitchDeltaNanoseconds;
}

/// Wall time the simulation is allowed to consume for one rendered frame. Ordinary jitter (missed
/// one or two VBlanks) is caught up as real time. A hitch, however, is charged as exactly ONE ...
[[nodiscard]] constexpr std::uint64_t SimulatedDelta(std::uint64_t raw_delta_ns) noexcept {
  if (IsHitch(raw_delta_ns)) {
    return GameRules::Simulation::kFixedStepNanoseconds;
  }
  return std::min(raw_delta_ns, GameRules::Simulation::kMaxFrameDeltaNanoseconds);
}

/// Adds one frame of wall time to the accumulator, bounded by the spiral-of-death
/// ceiling (at most kMaxStepsPerFrame steps of backlog can ever exist).
[[nodiscard]] constexpr std::uint64_t AccumulateBounded(std::uint64_t accumulator_ns,
                                                        std::uint64_t raw_delta_ns) noexcept {
  return std::min(accumulator_ns + SimulatedDelta(raw_delta_ns),
                  GameRules::Simulation::kMaxAccumulatorNanoseconds);
}

/// Accumulator level required to run the next fixed step. Only the FIRST step of a frame enjoys the
/// VBlank jitter slack, so a 60 Hz display does not flip between 0-step and 2-step frames when ...
[[nodiscard]] constexpr std::uint64_t StepThreshold(int steps_taken_this_frame) noexcept {
  constexpr std::uint64_t kStep = GameRules::Simulation::kFixedStepNanoseconds;
  constexpr std::uint64_t kSlack = GameRules::Simulation::kJitterSlackNanoseconds;
  return (steps_taken_this_frame == 0 && kStep > kSlack) ? (kStep - kSlack) : kStep;
}

/// Deducts one fixed step while keeping the sub-step remainder, which is what
/// feeds the render interpolation alpha. Never underflows.
[[nodiscard]] constexpr std::uint64_t ConsumeStep(std::uint64_t accumulator_ns) noexcept {
  constexpr std::uint64_t kStep = GameRules::Simulation::kFixedStepNanoseconds;
  return (accumulator_ns >= kStep) ? (accumulator_ns - kStep) : 0ULL;
}

/// Fraction of the next fixed step already elapsed (render interpolation alpha).
[[nodiscard]] constexpr float InterpolationAlpha(std::uint64_t accumulator_ns) noexcept {
  return static_cast<float>(
      static_cast<double>(accumulator_ns) /
      static_cast<double>(GameRules::Simulation::kFixedStepNanoseconds));
}

}  // namespace Pacing

#endif  // FRAME_PACING_H
