#include "explosion.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <vector>

#include "config.h"

void ExplosionsManager::Clear() noexcept {
  for (auto& p : pool_) p.active = false;
}

void ExplosionsManager::Add(Coord pos, Coord speed, uint8_t r, uint8_t g, uint8_t b, int duration) {
  const int count = std::clamp(Config::Instance().ScaleDetails(14 * duration), 6, 80);

  int spawned = 0;
  for (auto& p : pool_) {
    if (!p.active) {
      const float off_x = static_cast<float>(rng_.UniformInt(-26, 26));
      const float off_y = static_cast<float>(rng_.UniformInt(-26, 26));
      p.pos = Vec2f(static_cast<float>(pos.x) + off_x, static_cast<float>(pos.y) + off_y);

      const float jitter_x = static_cast<float>(rng_.UniformInt(-2, 2));
      const float jitter_y = static_cast<float>(rng_.UniformInt(-2, 2));
      const float vx = (off_x * 0.22f) + static_cast<float>(speed.x) + jitter_x;
      const float vy = (off_y * 0.22f) + static_cast<float>(speed.y) + jitter_y;
      p.vel = Vec2f(vx, vy);

      p.r = r;
      p.g = g;
      p.b = b;
      p.a = 255;
      p.life = p.max_life = static_cast<uint16_t>(duration + rng_.UniformInt(-4, 4));
      p.active = true;

      if (++spawned >= count) break;
    }
  }
}

void ExplosionsManager::TriggerScreenWideNova(Coord epicenter, int win_w, int win_h) {
  Add(epicenter, Coord(0, 0), 255, 255, 240, 75);
  Add(epicenter, Coord(0, -2), 255, 180, 40, 65);
  Add(epicenter, Coord(0, 2), 255, 50, 20, 65);

  for (auto& p : pool_) {
    if (!p.active) {
      const float ang = rng_.UniformFloat(0.0f, 6.2831853f);
      const float spd = rng_.UniformFloat(2.0f, 10.0f);
      p.pos = epicenter.ToVec2f();
      p.vel = Vec2f(std::cos(ang) * spd, std::sin(ang) * spd);
      p.r = 255;
      p.g = static_cast<uint8_t>(rng_.UniformInt(100, 240));
      p.b = 30;
      p.a = 255;
      p.life = p.max_life = static_cast<uint16_t>(rng_.UniformInt(40, 75));
      p.active = true;
    }
  }

  for (int x = 80; x < win_w; x += 220) {
    for (int y = 80; y < win_h; y += 180) {
      Add(Coord(x + rng_.UniformInt(-30, 30), y + rng_.UniformInt(-30, 30)),
          Coord(rng_.UniformInt(-2, 2), rng_.UniformInt(-2, 2)),
          255, 120, 20, 45);
    }
  }
}

void ExplosionsManager::Move() noexcept {
  for (auto& p : pool_) {
    if (!p.active) continue;
    p.pos += p.vel;
    p.vel *= 0.96f;

    if (p.life > 0) {
      --p.life;
      const float ratio = static_cast<float>(p.life) / static_cast<float>(p.max_life);
      p.a = static_cast<uint8_t>(ratio * 255.0f);
      p.r = static_cast<uint8_t>(static_cast<float>(p.r) * 0.985f);
      p.g = static_cast<uint8_t>(static_cast<float>(p.g) * 0.955f);
      p.b = static_cast<uint8_t>(static_cast<float>(p.b) * 0.955f);
    } else {
      p.active = false;
    }
  }
}

void ExplosionsManager::Draw() const {
  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;

  static thread_local std::vector<SDL_FPoint> pts_bright;
  static thread_local std::vector<SDL_FPoint> pts_orange;
  static thread_local std::vector<SDL_FPoint> pts_red;
  static thread_local std::vector<SDL_FPoint> pts_dark;

  pts_bright.clear();
  pts_orange.clear();
  pts_red.clear();
  pts_dark.clear();

  for (const auto& p : pool_) {
    if (!p.active || p.a == 0) continue;

    const float ratio = (p.max_life > 0) ? (static_cast<float>(p.life) / static_cast<float>(p.max_life)) : 0.0f;
    auto* target = (ratio > 0.65f) ? &pts_bright :
                   (ratio > 0.38f) ? &pts_orange :
                   (ratio > 0.15f) ? &pts_red : &pts_dark;

    target->push_back({p.pos.x, p.pos.y});
    target->push_back({p.pos.x + 1.0f, p.pos.y});
    target->push_back({p.pos.x, p.pos.y + 1.0f});
  }

  if (!pts_bright.empty()) {
    SDL_SetRenderDrawColor(renderer, 255, 245, 200, 255);
    SDL_RenderPoints(renderer, pts_bright.data(), static_cast<int>(pts_bright.size()));
  }
  if (!pts_orange.empty()) {
    SDL_SetRenderDrawColor(renderer, 255, 145, 35, 220);
    SDL_RenderPoints(renderer, pts_orange.data(), static_cast<int>(pts_orange.size()));
  }
  if (!pts_red.empty()) {
    SDL_SetRenderDrawColor(renderer, 230, 60, 25, 180);
    SDL_RenderPoints(renderer, pts_red.data(), static_cast<int>(pts_red.size()));
  }
  if (!pts_dark.empty()) {
    SDL_SetRenderDrawColor(renderer, 120, 35, 20, 110);
    SDL_RenderPoints(renderer, pts_dark.data(), static_cast<int>(pts_dark.size()));
  }
}
