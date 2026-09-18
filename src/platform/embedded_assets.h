#ifndef EMBEDDED_ASSETS_H
#define EMBEDDED_ASSETS_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "math_types.h"

enum class TextureId : uint8_t {
  None = 0,
  Player,
  PlayerAlt,
  Bullet,
  Bomb,
  BonusFire,
  BonusShield,
  BonusSpeed,
  BonusMulti,
  BonusNuke,
  Alien1,
  Alien2,
  Alien3,
  Alien4,
  Alien5,
  Alien6,
  Alien7,
  Alien8,
  Alien9,
  Alien10,
  Alien11,
  Alien12,
  Alien13,
  Alien14,
  Alien15,
  Count
};

namespace EmbeddedImages {
std::pair<std::span<const uint8_t>, Coord> GetTextureData(TextureId id);
}

#endif  // EMBEDDED_ASSETS_H
