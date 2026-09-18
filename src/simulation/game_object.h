#ifndef SIMULATION_GAME_OBJECT_H
#define SIMULATION_GAME_OBJECT_H

#include "collider.h"
#include "motion.h"
#include "renderable.h"
#include "transform.h"

namespace simulation {

struct GameObject {
  Transform transform{};
  Motion motion{};
  Collider collider{};
  Renderable renderable{};
  bool active{true};

  [[nodiscard]] Coord Position() const noexcept { return transform.position; }
  [[nodiscard]] int Width() const noexcept { return renderable.pix ? renderable.pix->Width() : 0; }
  [[nodiscard]] int Height() const noexcept { return renderable.pix ? renderable.pix->Height() : 0; }
  [[nodiscard]] Coord Dim() const noexcept { return renderable.pix ? renderable.pix->Dim() : Coord(0, 0); }

  void Update() noexcept {
    if (active) transform.position = motion.Integrate(transform.position);
  }

  void Draw() const {
    if (active) renderable.Draw(transform);
  }
  void Draw(float extra_angle) const {
    if (active && renderable.pix) renderable.pix->Draw(transform.position, transform.rotation_deg + extra_angle);
  }

  [[nodiscard]] bool Intersects(const GameObject& other, float shrink = 0.65f) const noexcept {
    return collider.Intersects(transform, other.collider, other.transform, shrink);
  }
};

}  // namespace simulation

#endif  // SIMULATION_GAME_OBJECT_H
