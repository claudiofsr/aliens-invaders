#ifndef RANDOM_STREAM_H
#define RANDOM_STREAM_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

/**
 * @class RandomStream
 * @brief Deterministic, zero-allocation PRNG for pure gameplay logic.
 * Guarantees that random calculations never lock global mutexes or cross thread boundaries.
 */
class RandomStream final {
 public:
  explicit constexpr RandomStream(std::uint32_t seed = 0xA341316Cu) noexcept
      : state_(seed == 0u ? 0xA341316Cu : seed) {}

  constexpr void Seed(std::uint32_t seed) noexcept {
    state_ = (seed == 0u ? 0xA341316Cu : seed);
  }

  [[nodiscard]] std::uint32_t NextU32() noexcept {
    std::uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = (x == 0u ? 0xA341316Cu : x);
    return state_;
  }

  [[nodiscard]] int UniformInt(int min_value, int max_value) noexcept {
    if (min_value > max_value) std::swap(min_value, max_value);
    const std::uint64_t span =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(max_value) -
                                   static_cast<std::int64_t>(min_value)) + 1u;
    const std::uint64_t limit =
        (static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1u) /
        span * span;
    std::uint32_t value = 0u;
    do {
      value = NextU32();
    } while (static_cast<std::uint64_t>(value) >= limit);
    return min_value + static_cast<int>(static_cast<std::uint64_t>(value) % span);
  }

  [[nodiscard]] float UniformFloat(float min_value, float max_value) noexcept {
    if (min_value > max_value) std::swap(min_value, max_value);
    constexpr float kInvU32 = 1.0f / 4294967295.0f;
    const float unit = static_cast<float>(NextU32()) * kInvU32;
    return min_value + (max_value - min_value) * unit;
  }

  [[nodiscard]] bool Chance(int numerator, int denominator) noexcept {
    if (denominator <= 0 || numerator <= 0) return false;
    if (numerator >= denominator) return true;
    return UniformInt(1, denominator) <= numerator;
  }

  // Satisfies UniformRandomBitGenerator, so RandomStream can be used
  // directly with <random> distributions (std::uniform_int_distribution,
  // std::bernoulli_distribution, std::shuffle) without ever exposing a
  // std::mt19937 or a std::random_device anywhere in gameplay code.
  using result_type = std::uint32_t;
  [[nodiscard]] static constexpr result_type min() noexcept { return 0u; }
  [[nodiscard]] static constexpr result_type max() noexcept {
    return std::numeric_limits<std::uint32_t>::max();
  }
  [[nodiscard]] result_type operator()() noexcept { return NextU32(); }

  [[nodiscard]] bool Bernoulli(float probability) noexcept {
    if (probability <= 0.0f) return false;
    if (probability >= 1.0f) return true;
    return UniformFloat(0.0f, 1.0f) < probability;
  }

  [[nodiscard]] float Cauchy(float median, float scale) noexcept {
    constexpr float kPi = 3.14159265358979323846f;
    const float u = UniformFloat(0.0001f, 0.9999f);
    return median + scale * std::tan(kPi * (u - 0.5f));
  }

 private:
  std::uint32_t state_;
};

#endif  // RANDOM_STREAM_H
