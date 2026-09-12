#include "explosion.h"

#include <algorithm>
#include <cstdlib>

#include "config.h"

Explosion::Explosion(Coord pos, Coord speed, uint8_t r, uint8_t g, uint8_t b,
                     int duration) {
  Reset(pos, speed, r, g, b, duration);
}

void Explosion::Reset(Coord pos, Coord speed, uint8_t r, uint8_t g, uint8_t b,
                      int duration) {
  r_ = r;
  g_ = g;
  b_ = b;
  moves_ = 0;
  duration_ = duration;
  active_ = true;

  const int requested = Config::Instance().ScaleDetails(16 * duration);
  nb_dots_ = std::min(MAX_DOTS, static_cast<size_t>(std::max(0, requested)));

  for (size_t i = 0; i < nb_dots_; ++i) {
    const Coord particle(std::rand() % 32 + std::rand() % 32 - 32,
                         std::rand() % 32 + std::rand() % 32 - 32);
    dots_[i] = particle + pos;
    Coord particle_speed = particle / 4 + speed;
    particle_speed += Coord(std::rand() % 5 - 2, std::rand() % 5 - 2);
    dots_speed_[i] = particle_speed;
  }
}

void Explosion::Move() {
  if (!active_) return;
  if (moves_ < duration_) {
    ++moves_;
    for (size_t i = 0; i < nb_dots_; ++i) {
      dots_[i] += dots_speed_[i];
    }
    r_ = static_cast<uint8_t>(r_ * 98 / 100);
    g_ = static_cast<uint8_t>(g_ * 95 / 100);
    b_ = static_cast<uint8_t>(b_ * 95 / 100);
  } else {
    active_ = false;
  }
}

void Explosion::Draw() const {
  if (!active_ || moves_ >= duration_ || nb_dots_ == 0) return;
  Gfx::Inst().SetDrawColor(r_, g_, b_, 255);
  Gfx::Inst().DrawPoints(dots_.data(), nb_dots_);
}
