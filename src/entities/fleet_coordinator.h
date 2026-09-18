#ifndef FLEET_COORDINATOR_H
#define FLEET_COORDINATOR_H

#include <cmath>
#include <vector>

#include "math_types.h"

/**
 * @struct SplineKnot
 * @brief Hermite/B-Spline node defining continuous curvature flight paths.
 */
struct SplineKnot {
  Vec2f position;
  Vec2f tangent;
};

/**
 * @class FleetFormation
 * @brief Clean-room mathematical fleet formulation for commercial distribution.
 * Original parametric fleet math - 100% original IP
 */
class FleetFormation {
 public:
  [[nodiscard]] static Vec2f EvaluateCubicHermite(const SplineKnot& p0,
                                                  const SplineKnot& p1,
                                                  float t) noexcept {
    const float t2 = t * t;
    const float t3 = t2 * t;

    const float h1 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h2 = -2.0f * t3 + 3.0f * t2;
    const float h3 = t3 - 2.0f * t2 + t;
    const float h4 = t3 - t2;

    return {h1 * p0.position.x + h2 * p1.position.x + h3 * p0.tangent.x +
                h4 * p1.tangent.x,
            h1 * p0.position.y + h2 * p1.position.y + h3 * p0.tangent.y +
                h4 * p1.tangent.y};
  }

  [[nodiscard]] static std::vector<Vec2f> BuildParametricDive(
      const std::vector<SplineKnot>& knots, size_t samples_per_segment = 32) {
    std::vector<Vec2f> path;
    if (knots.size() < 2) return path;

    path.reserve((knots.size() - 1) * samples_per_segment + 1);
    for (size_t i = 0; i < knots.size() - 1; ++i) {
      for (size_t s = 0; s < samples_per_segment; ++s) {
        const float t =
            static_cast<float>(s) / static_cast<float>(samples_per_segment);
        path.push_back(EvaluateCubicHermite(knots[i], knots[i + 1], t));
      }
    }
    path.push_back(knots.back().position);
    return path;
  }
};

#endif  // FLEET_COORDINATOR_H
