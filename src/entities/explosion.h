#ifndef EXPLOSION_H
#define EXPLOSION_H

#include <array>
#include <cstdint>

#include "gfxinterface.h"

class Explosion {
  static constexpr size_t MAX_DOTS = 512;
  uint8_t r_{255}, g_{255}, b_{255};
  int moves_{0};
  int duration_{24};
  size_t nb_dots_{0};
  std::array<Coord, MAX_DOTS> dots_{};
  std::array<Coord, MAX_DOTS> dots_speed_{};
  bool active_{false};

 public:
  Explosion() = default;
  Explosion(Coord pos, Coord speed, uint8_t r, uint8_t g, uint8_t b,
            int duration = 24);

  void Reset(Coord pos, Coord speed, uint8_t r, uint8_t g, uint8_t b,
             int duration = 24);
  void Move();
  void Draw() const;
  [[nodiscard]] bool Active() const noexcept { return active_; }
  [[nodiscard]] bool Finished() const noexcept {
    return !active_ || moves_ >= duration_;
  }
};

#endif  // EXPLOSION_H
