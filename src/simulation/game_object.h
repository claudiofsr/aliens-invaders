#ifndef SIMULATION_GAME_OBJECT_H
#define SIMULATION_GAME_OBJECT_H

#include "collider.h"
#include "constants.h"
#include "motion.h"
#include "renderable.h"
#include "transform.h"
#include "math_types.h"

namespace simulation {

struct GameObject {
  Transform transform{};
  Motion motion{};
  Collider collider{};
  Renderable renderable{};
  bool active{true};

  float scale{1.0f};

  [[nodiscard]] Coord Position() const noexcept { return transform.position; }
  [[nodiscard]] int Width() const noexcept {
    const int base_w = renderable.pix ? renderable.pix->Width() : 0;
    return (scale == 1.0f) ? base_w : FastRound(static_cast<float>(base_w) * scale);
  }
  [[nodiscard]] int Height() const noexcept {
    const int base_h = renderable.pix ? renderable.pix->Height() : 0;
    return (scale == 1.0f) ? base_h : FastRound(static_cast<float>(base_h) * scale);
  }
  void Update() noexcept {
    if (active) transform.position = motion.Integrate(transform.position);
  }

  [[nodiscard]] bool Intersects(
      const GameObject& other,
      float shrink = GameRules::Player::kCollisionHitboxScale) const noexcept {
    return collider.Intersects(transform, other.collider, other.transform, shrink);
  }
};

}  // namespace simulation

#endif  // SIMULATION_GAME_OBJECT_H
