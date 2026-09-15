#ifndef EXPLOSION_H
#define EXPLOSION_H

#include <array>
#include <cstdint>
#include "gfxinterface.h"
#include "random_stream.h"

struct Particle {
  Vec2f pos{0.0f, 0.0f};
  Vec2f vel{0.0f, 0.0f};
  uint8_t r{255};
  uint8_t g{255};
  uint8_t b{255};
  uint8_t a{255};
  uint16_t life{0};
  uint16_t max_life{0};
  bool active{false};
};

class ExplosionsManager {
  static constexpr size_t kMaxParticles = 4096;
  std::array<Particle, kMaxParticles> pool_{};
  RandomStream rng_{0x5EED1337u};

 public:
  ExplosionsManager() = default;
  ~ExplosionsManager() = default;

  ExplosionsManager(const ExplosionsManager&) = delete;
  ExplosionsManager& operator=(const ExplosionsManager&) = delete;
  ExplosionsManager(ExplosionsManager&&) noexcept = default;
  ExplosionsManager& operator=(ExplosionsManager&&) noexcept = default;

  void Move() noexcept;
  void Draw() const;
  void Clear() noexcept;

  void Add(Coord pos, Coord speed, uint8_t r, uint8_t g, uint8_t b, int duration = 24);
  void TriggerScreenWideNova(Coord epicenter, int win_w, int win_h);
};

#endif  // EXPLOSION_H
