#ifndef SIMULATION_MOTION_H
#define SIMULATION_MOTION_H

#include <algorithm>
#include <cmath>

#include "math_types.h"
#include "transform.h"

namespace simulation {

struct Motion {
  Coord velocity{};

  constexpr Motion() noexcept = default;
  constexpr explicit Motion(Coord vel) noexcept : velocity(vel) {}

  [[nodiscard]] constexpr Coord Integrate(Coord position) const noexcept {
    return position + velocity;
  }
};

/// One rate-limited ease-out turn slice (angular partitioning): advances `angle_deg` toward
/// `target_deg` along the shortest arc. The slice is proportional to the remaining arc ...
[[nodiscard]] inline float AdvanceTurnDeg(float angle_deg, float target_deg,
                                          float response, float snap_deg,
                                          float cap_deg) noexcept {
  float diff = target_deg - angle_deg;
  while (diff < -180.0f) diff += 360.0f;
  while (diff > 180.0f) diff -= 360.0f;
  const float step = std::clamp(std::abs(diff) * response, snap_deg, cap_deg);
  float next = angle_deg + ((std::abs(diff) <= step) ? diff
                                                     : std::copysign(step, diff));
  while (next > 180.0f) next -= 360.0f;
  while (next < -180.0f) next += 360.0f;
  return next;
}

/// Delta Jump Guard arithmetic: rescales the step `from -> to` so that no axis exceeds `budget`
/// (Chebyshev clamp, direction preserved). A non-positive budget freezes the position; a step ...
[[nodiscard]] inline Vec2f ClampStepToBudget(Vec2f from, Vec2f to,
                                             float budget) noexcept {
  if (budget <= 0.0f) return from;
  const Vec2f jump = to - from;
  const float max_axis = std::max(std::abs(jump.x), std::abs(jump.y));
  if (max_axis <= budget) return to;
  return from + jump * (budget / max_axis);
}

/// Shared pursuit primitive: advances `from` toward `to` by at most `step` (Euclidean), landing
/// EXACTLY on `to` inside one step so a chase can never orbit or overshoot its destination ...
[[nodiscard]] inline Vec2f AdvanceToward(Vec2f from, Vec2f to,
                                         float step) noexcept {
  const Vec2f delta = to - from;
  const float dist = delta.Length();
  if (dist <= step || dist <= 0.0f) return to;
  return from + delta * (step / dist);
}

}  // namespace simulation

#endif  // SIMULATION_MOTION_H
