#ifndef MATH_TYPES_H
#define MATH_TYPES_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>

#include "constants.h"

/** @struct Vec2f @brief High-performance 2D floating-point vector with subpixel precision. */
struct Vec2f {
  float x{0.0f};
  float y{0.0f};

  constexpr Vec2f() noexcept = default;
  constexpr Vec2f(float nx, float ny) noexcept : x(nx), y(ny) {}

  [[nodiscard]] constexpr Vec2f operator+(const Vec2f& rhs) const noexcept { return {x + rhs.x, y + rhs.y}; }
  [[nodiscard]] constexpr Vec2f operator-(const Vec2f& rhs) const noexcept { return {x - rhs.x, y - rhs.y}; }
  [[nodiscard]] constexpr Vec2f operator*(float s) const noexcept { return {x * s, y * s}; }
  [[nodiscard]] constexpr Vec2f operator/(float s) const noexcept {
    const float inv = 1.0f / s;
    return {x * inv, y * inv};
  }

  constexpr Vec2f& operator+=(const Vec2f& rhs) noexcept {
    x += rhs.x;
    y += rhs.y;
    return *this;
  }
  constexpr Vec2f& operator-=(const Vec2f& rhs) noexcept {
    x -= rhs.x;
    y -= rhs.y;
    return *this;
  }
  constexpr Vec2f& operator*=(float s) noexcept {
    x *= s;
    y *= s;
    return *this;
  }
  constexpr Vec2f& operator/=(float s) noexcept {
    const float inv = 1.0f / s;
    x *= inv;
    y *= inv;
    return *this;
  }

  [[nodiscard]] constexpr float LengthSquared() const noexcept { return x * x + y * y; }
  [[nodiscard]] float Length() const noexcept { return std::sqrt(LengthSquared()); }

  [[nodiscard]] static constexpr Vec2f Lerp(const Vec2f& a, const Vec2f& b, float t) noexcept {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
  }
};

inline constexpr Vec2f operator*(float s, const Vec2f& v) noexcept { return v * s; }

/// Round-half-away-from-zero helper shared by every Coord conversion.
[[nodiscard]] inline constexpr int32_t FastRound(float v) noexcept {
  return static_cast<int32_t>(v >= 0.0f ? (v + 0.5f) : (v - 0.5f));
}

[[nodiscard]] inline constexpr int32_t FastRound(double v) noexcept {
  return static_cast<int32_t>(v >= 0.0 ? (v + 0.5) : (v - 0.5));
}

/// Robust 32-bit discrete screen-space coordinate: prevents 16-bit integer
/// overflow on 4K/8K UHD resolutions.
struct Coord {
  int32_t x{0};
  int32_t y{0};

  constexpr Coord() noexcept = default;
  constexpr Coord(int32_t _x, int32_t _y) noexcept : x(_x), y(_y) {}
  constexpr Coord(float _x, float _y) noexcept
      : x(FastRound(_x)), y(FastRound(_y)) {}
  constexpr Coord(double _x, double _y) noexcept
      : x(FastRound(_x)), y(FastRound(_y)) {}
  constexpr Coord(const Vec2f& v) noexcept
      : x(FastRound(v.x)), y(FastRound(v.y)) {}

  [[nodiscard]] constexpr Vec2f ToVec2f() const noexcept {
    return {static_cast<float>(x), static_cast<float>(y)};
  }
  [[nodiscard]] double Length() const noexcept {
    // Promote to double BEFORE multiplying: the int32 products overflow (UB) for magnitudes beyond
    // ~46340 -- exactly the UHD headroom this "robust 32-bit" coordinate type exists for.
    return std::sqrt(static_cast<double>(x) * static_cast<double>(x) +
                     static_cast<double>(y) * static_cast<double>(y));
  }

  constexpr Coord& operator+=(Coord rhs) noexcept {
    x += rhs.x;
    y += rhs.y;
    return *this;
  }
  constexpr Coord& operator-=(Coord rhs) noexcept {
    x -= rhs.x;
    y -= rhs.y;
    return *this;
  }
  constexpr Coord& operator*=(double rhs) noexcept {
    x = FastRound(static_cast<double>(x) * rhs);
    y = FastRound(static_cast<double>(y) * rhs);
    return *this;
  }
  constexpr Coord& operator/=(double rhs) noexcept {
    const double inv = 1.0 / rhs;
    x = FastRound(static_cast<double>(x) * inv);
    y = FastRound(static_cast<double>(y) * inv);
    return *this;
  }
  constexpr Coord& operator*=(int32_t rhs) noexcept {
    x *= rhs;
    y *= rhs;
    return *this;
  }
  constexpr Coord& operator/=(int32_t rhs) noexcept {
    x /= rhs;
    y /= rhs;
    return *this;
  }
};

inline constexpr Coord operator+(Coord lhs, Coord rhs) noexcept { return lhs += rhs; }
inline constexpr Coord operator-(Coord lhs, Coord rhs) noexcept { return lhs -= rhs; }
template <class T>
inline constexpr Coord operator*(T lhs, Coord rhs) noexcept { return rhs *= lhs; }
template <class T>
inline constexpr Coord operator/(Coord lhs, T rhs) noexcept { return lhs /= rhs; }
inline constexpr bool operator==(Coord lhs, Coord rhs) noexcept { return lhs.x == rhs.x && lhs.y == rhs.y; }
inline constexpr bool operator!=(Coord lhs, Coord rhs) noexcept { return !(lhs == rhs); }
inline constexpr bool operator<(Coord lhs, Coord rhs) noexcept { return std::tie(lhs.x, lhs.y) < std::tie(rhs.x, rhs.y); }

/** @struct Transform2D @brief Decoupled spatial state component enabling subpixel interpolation. */
struct Transform2D {
  Vec2f position{0.0f, 0.0f};
  Vec2f previous_position{0.0f, 0.0f};
  Vec2f velocity{0.0f, 0.0f};
  float rotation_deg{0.0f};
  float previous_rotation_deg{0.0f};
  float scale{1.0f};

  constexpr Transform2D() noexcept = default;
  constexpr Transform2D(Vec2f pos, float rot = 0.0f, float sc = 1.0f) noexcept
      : position(pos), previous_position(pos), rotation_deg(rot), previous_rotation_deg(rot), scale(sc) {}

  void Step(float dt = 1.0f) noexcept {
    previous_position = position;
    previous_rotation_deg = rotation_deg;
    position += velocity * dt;
  }

  [[nodiscard]] Vec2f InterpolatedPosition(float alpha) const noexcept {
    return Vec2f::Lerp(previous_position, position, alpha);
  }
};

/** @struct ViewportMetrics @brief Display metrics snapshot injected into the pure simulation core.
 * The graphics boundary captures it (construction/resize only); kinematics reads this POD ... */
struct ViewportMetrics {
  int win_w{1280};
  int win_h{720};
  float scale{1.0f};
};

/** @struct AABB @brief Axis-Aligned Bounding Box component for decoupled collision checks. */
struct AABB {
  Vec2f half_extents{0.0f, 0.0f};

  constexpr AABB() noexcept = default;
  constexpr AABB(float hw, float hh) noexcept : half_extents(hw, hh) {}

  [[nodiscard]] constexpr bool Intersects(Vec2f pos_a, Vec2f pos_b, const AABB& other, float hit_factor = GameRules::Player::kCollisionHitboxScale) const noexcept {
    // Vertical shooter optimization: check Y-band first to reject 85%+ of non-colliding entities
    // Core-language |d| (Chebyshev-style ternary): MSVC rejects std::abs in
    // constexpr (C3615) and older libc++ lacks P0533 -- one portable spelling.
    const float ha_y = half_extents.y * hit_factor;
    const float hb_y = other.half_extents.y * hit_factor;
    const float dy = pos_a.y - pos_b.y;
    if ((dy < 0.0f ? -dy : dy) > (ha_y + hb_y)) return false;

    const float ha_x = half_extents.x * hit_factor;
    const float hb_x = other.half_extents.x * hit_factor;
    const float dx = pos_a.x - pos_b.x;
    return (dx < 0.0f ? -dx : dx) <= (ha_x + hb_x);
  }
};


/** @brief Pure functional AABB collision query between two spatial transforms. Can be tested and
 * simulated completely headless without GPU or SDL textures. */
[[nodiscard]] inline constexpr bool CheckCollision(
    const Transform2D& a, const AABB& box_a,
    const Transform2D& b, const AABB& box_b,
    float hit_factor = GameRules::Player::kCollisionHitboxScale) noexcept {
  return box_a.Intersects(a.position, b.position, box_b, hit_factor);
}

/// Chebyshev distance (L-infinity norm) for grid-based spatial queries.
/// O(1) pure arithmetic, no sqrt, ideal for honeycomb exclusion zones.
[[nodiscard]] constexpr int ChebyshevDistance(int x1, int y1, int x2, int y2) noexcept {
    const int dx = (x1 > x2) ? (x1 - x2) : (x2 - x1);
    const int dy = (y1 > y2) ? (y1 - y2) : (y2 - y1);
    return (dx > dy) ? dx : dy;
}
[[nodiscard]] constexpr int ChebyshevDistance(const Coord& a, const Coord& b) noexcept {
    return ChebyshevDistance(a.x, a.y, b.x, b.y);
}

/// Integer division floored toward negative infinity. Plain C++ division truncates toward zero,
/// which would shift a derived cell index by one for positions left of (or above) the origin -- ...
[[nodiscard]] constexpr int FloorDiv(int numerator, int denominator) noexcept {
    if (denominator == 0) return 0;  // defensive: real callers pass cell spans >= 1
    const int quotient = numerator / denominator;
    const int remainder = numerator % denominator;
    return (remainder != 0 && ((numerator < 0) != (denominator < 0)))
               ? quotient - 1
               : quotient;
}
#endif  // MATH_TYPES_H
