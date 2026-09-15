#ifndef MATH_TYPES_H
#define MATH_TYPES_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>

/**
 * @struct Vec2f
 * @brief High-performance 2D floating-point vector with subpixel precision.
 */
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

  [[nodiscard]] Vec2f Normalized() const noexcept {
    const float len = Length();
    return (len > 1e-6f) ? (*this / len) : Vec2f{0.0f, 0.0f};
  }

  [[nodiscard]] constexpr Vec2f XMirror(float width = 1024.0f) const noexcept { return {width - x, y}; }

  [[nodiscard]] static constexpr Vec2f Lerp(const Vec2f& a, const Vec2f& b, float t) noexcept {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
  }
};

inline constexpr Vec2f operator*(float s, const Vec2f& v) noexcept { return v * s; }

/**
 * @struct Coord
 * @brief Robust 32-bit discrete screen-space coordinate.
 * Prevents 16-bit integer overflow on 4K/8K UHD resolutions.
 */
struct Coord {
  int32_t x{0};
  int32_t y{0};

  constexpr Coord() noexcept = default;
  constexpr Coord(int32_t _x, int32_t _y) noexcept : x(_x), y(_y) {}
  constexpr Coord(float _x, float _y) noexcept
      : x(static_cast<int32_t>(std::round(_x))), y(static_cast<int32_t>(std::round(_y))) {}
  constexpr Coord(double _x, double _y) noexcept
      : x(static_cast<int32_t>(std::round(_x))), y(static_cast<int32_t>(std::round(_y))) {}
  constexpr Coord(const Vec2f& v) noexcept
      : x(static_cast<int32_t>(std::round(v.x))), y(static_cast<int32_t>(std::round(v.y))) {}

  [[nodiscard]] constexpr Vec2f ToVec2f() const noexcept {
    return {static_cast<float>(x), static_cast<float>(y)};
  }
  [[nodiscard]] double Length() const noexcept {
    return std::sqrt(static_cast<double>(x * x + y * y));
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
    x = static_cast<int32_t>(std::round(static_cast<double>(x) * rhs));
    y = static_cast<int32_t>(std::round(static_cast<double>(y) * rhs));
    return *this;
  }
  constexpr Coord& operator/=(double rhs) noexcept {
    x = static_cast<int32_t>(std::round(static_cast<double>(x) / rhs));
    y = static_cast<int32_t>(std::round(static_cast<double>(y) / rhs));
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

/**
 * @struct Transform2D
 * @brief Decoupled spatial state component enabling subpixel interpolation.
 */
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

  [[nodiscard]] float InterpolatedRotation(float alpha) const noexcept {
    float diff = rotation_deg - previous_rotation_deg;
    while (diff < -180.0f) diff += 360.0f;
    while (diff > 180.0f) diff -= 360.0f;
    return previous_rotation_deg + diff * alpha;
  }
};

/**
 * @struct AABB
 * @brief Axis-Aligned Bounding Box component for decoupled collision checks.
 */
struct AABB {
  Vec2f half_extents{0.0f, 0.0f};

  constexpr AABB() noexcept = default;
  constexpr AABB(float hw, float hh) noexcept : half_extents(hw, hh) {}

  [[nodiscard]] constexpr bool Intersects(Vec2f pos_a, Vec2f pos_b, const AABB& other, float hit_factor = 0.65f) const noexcept {
    const float ha_x = half_extents.x * hit_factor;
    const float ha_y = half_extents.y * hit_factor;
    const float hb_x = other.half_extents.x * hit_factor;
    const float hb_y = other.half_extents.y * hit_factor;
    return (std::abs(pos_a.x - pos_b.x) <= (ha_x + hb_x)) &&
           (std::abs(pos_a.y - pos_b.y) <= (ha_y + hb_y));
  }
};


/**
 * @brief Pure functional AABB collision query between two spatial transforms.
 * Can be tested and simulated completely headless without GPU or SDL textures.
 */
[[nodiscard]] inline constexpr bool CheckCollision(
    const Transform2D& a, const AABB& box_a,
    const Transform2D& b, const AABB& box_b,
    float hit_factor = 0.65f) noexcept {
  return box_a.Intersects(a.position, b.position, box_b, hit_factor);
}

#endif  // MATH_TYPES_H
