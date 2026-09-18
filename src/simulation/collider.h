#ifndef SIMULATION_COLLIDER_H
#define SIMULATION_COLLIDER_H

#include "math_types.h"
#include "transform.h"

namespace simulation {

struct Collider {
  AABB aabb{};

  constexpr Collider() noexcept = default;
  constexpr explicit Collider(AABB box) noexcept : aabb(box) {}
  constexpr Collider(float hw, float hh) noexcept : aabb(hw, hh) {}

  [[nodiscard]] bool Intersects(const Transform& self_transform,
                                const Collider& other,
                                const Transform& other_transform,
                                float shrink = 0.65f) const noexcept {
    return aabb.Intersects(self_transform.position.ToVec2f(),
                           other_transform.position.ToVec2f(),
                           other.aabb, shrink);
  }
};

}  // namespace simulation

#endif  // SIMULATION_COLLIDER_H
