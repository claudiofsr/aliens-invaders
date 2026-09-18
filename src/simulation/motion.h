#ifndef SIMULATION_MOTION_H
#define SIMULATION_MOTION_H

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

}  // namespace simulation

#endif  // SIMULATION_MOTION_H
