#ifndef MATH_TYPES_H
#define MATH_TYPES_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>

/**
 * @struct Vec2f
 * @brief High-performance 2D floating-point vector for subpixel physics and
 * spline interpolation. Guarantees standard layout and zero-overhead SIMD
 * vectorization.
 */
struct Vec2f {
  float x{0.0f};
  float y{0.0f};

  constexpr Vec2f() noexcept = default;
  constexpr Vec2f(float nx, float ny) noexcept : x(nx), y(ny) {}

  [[nodiscard]] constexpr Vec2f operator+(const Vec2f& rhs) const noexcept {
    return {x + rhs.x, y + rhs.y};
  }
  [[nodiscard]] constexpr Vec2f operator-(const Vec2f& rhs) const noexcept {
    return {x - rhs.x, y - rhs.y};
  }
  [[nodiscard]] constexpr Vec2f operator*(float s) const noexcept {
    return {x * s, y * s};
  }
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

  [[nodiscard]] constexpr float LengthSquared() const noexcept {
    return x * x + y * y;
  }
  [[nodiscard]] float Length() const noexcept {
    return std::sqrt(LengthSquared());
  }

  [[nodiscard]] Vec2f Normalized() const noexcept {
    const float len = Length();
    return (len > 1e-6f) ? (*this / len) : Vec2f{0.0f, 0.0f};
  }

  [[nodiscard]] constexpr Vec2f XMirror(float width = 1024.0f) const noexcept {
    return {width - x, y};
  }
};

inline constexpr Vec2f operator*(float s, const Vec2f& v) noexcept {
  return v * s;
}

/**
 * @struct Coord
 * @brief Screen-space 2D integer coordinate used for discrete rendering and
 * sprite geometry.
 */
struct Coord {
  short x{0};
  short y{0};

  constexpr Coord() noexcept = default;
  constexpr Coord(short _x, short _y) noexcept : x(_x), y(_y) {}
  constexpr Coord(int _x, int _y) noexcept
      : x(static_cast<short>(_x)), y(static_cast<short>(_y)) {}
  constexpr Coord(float _x, float _y) noexcept
      : x(static_cast<short>(std::round(_x))),
        y(static_cast<short>(std::round(_y))) {}
  constexpr Coord(const Vec2f& v) noexcept
      : x(static_cast<short>(std::round(v.x))),
        y(static_cast<short>(std::round(v.y))) {}

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
    x = static_cast<short>(std::floor(x * rhs + 0.5));
    y = static_cast<short>(std::floor(y * rhs + 0.5));
    return *this;
  }
  constexpr Coord& operator/=(double rhs) noexcept {
    x = static_cast<short>(std::floor(x / rhs + 0.5));
    y = static_cast<short>(std::floor(y / rhs + 0.5));
    return *this;
  }
  constexpr Coord& operator*=(int rhs) noexcept {
    x = static_cast<short>(x * rhs);
    y = static_cast<short>(y * rhs);
    return *this;
  }
  constexpr Coord& operator/=(int rhs) noexcept {
    x = static_cast<short>(x / rhs);
    y = static_cast<short>(y / rhs);
    return *this;
  }
};

inline constexpr Coord operator+(Coord lhs, Coord rhs) noexcept {
  return lhs += rhs;
}
inline constexpr Coord operator-(Coord lhs, Coord rhs) noexcept {
  return lhs -= rhs;
}
template <class T>
inline constexpr Coord operator*(T lhs, Coord rhs) noexcept {
  return rhs *= lhs;
}
template <class T>
inline constexpr Coord operator/(Coord lhs, T rhs) noexcept {
  return lhs /= rhs;
}
inline constexpr bool operator==(Coord lhs, Coord rhs) noexcept {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}
inline constexpr bool operator!=(Coord lhs, Coord rhs) noexcept {
  return !(lhs == rhs);
}
inline constexpr bool operator<(Coord lhs, Coord rhs) noexcept {
  return std::tie(lhs.x, lhs.y) < std::tie(rhs.x, rhs.y);
}

#endif  // MATH_TYPES_H
