#ifndef SIMULATION_RENDERABLE_H
#define SIMULATION_RENDERABLE_H

#include "transform.h"
#include "sdl_renderer.h"

namespace simulation {

struct Renderable {
  const Pix* pix{nullptr};

  constexpr Renderable() noexcept = default;
};

}  // namespace simulation

#endif  // SIMULATION_RENDERABLE_H
