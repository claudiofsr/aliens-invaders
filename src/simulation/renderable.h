#ifndef SIMULATION_RENDERABLE_H
#define SIMULATION_RENDERABLE_H

#include "pix.h"
#include "transform.h"

namespace simulation {

struct Renderable {
  const Pix* pix{nullptr};

  constexpr Renderable() noexcept = default;
  constexpr explicit Renderable(const Pix* p) noexcept : pix(p) {}

  void Draw(const Transform& transform) const {
    if (pix) pix->Draw(transform.position, transform.rotation_deg);
  }
};

}  // namespace simulation

#endif  // SIMULATION_RENDERABLE_H
