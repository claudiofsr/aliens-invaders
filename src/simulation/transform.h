#ifndef SIMULATION_TRANSFORM_H
#define SIMULATION_TRANSFORM_H

#include "math_types.h"

namespace simulation {

struct Transform {
  Coord position{};
  float rotation_deg{0.0f};

  constexpr Transform() noexcept = default;
  constexpr Transform(Coord pos, float rot = 0.0f) noexcept
      : position(pos), rotation_deg(rot) {}
};

}  // namespace simulation

#endif  // SIMULATION_TRANSFORM_H
