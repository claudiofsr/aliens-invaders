#ifndef BAKE_HASH_H
#define BAKE_HASH_H

#include <cstddef>
#include <cstdint>
#include <string_view>

/** @namespace BakeCache @brief Pure hash & probe arithmetic for the baked-string texture cache. Why
 * a dedicated header (AGENT 5 / AGENT 7): - The baked-string lookup sits on the per-frame hot ... */
namespace BakeCache {

/// Probe-slot count of the open-addressing index. Power of two and at least 2x the cache capacity
/// (256 entries), so the load factor never exceeds 0.5 and linear-probe chains stay at 1-2 ...
inline constexpr std::size_t kIndexSlots = 512;

/// FNV-1a (64-bit) over the FULL identity of a cache entry: text, quantized pixel size, face weight
/// and shadow flag. Pure integer mixing -- no allocation, no I/O, deterministic across runs and ...
[[nodiscard]] constexpr std::uint64_t BakeHash(std::string_view text,
                                               int size_key, bool bold,
                                               bool shadow) noexcept {
  constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t h = kOffsetBasis;
  for (const char c : text) {
    h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
    h *= kPrime;
  }
  // Fold the non-text identity components into the same digest so the same
  // text at a different size / face / shadow can never alias onto an entry.
  h ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(size_key));
  h *= kPrime;
  h ^= (bold ? 0xB01Du : 0x5EEDu);
  h *= kPrime;
  h ^= (shadow ? 0x5AD0u : 0xC0DEu);
  h *= kPrime;
  return h;
}

/// First probe slot of a digest. kIndexSlots is a power of two, so the modulo
/// collapses to a single AND -- no division on the hot path.
[[nodiscard]] constexpr std::size_t SlotOf(std::uint64_t hash) noexcept {
  return static_cast<std::size_t>(hash) & (kIndexSlots - 1);
}

/// Next probe slot (linear probing, wraps with one AND). Insert and lookup walk the SAME sequence,
/// which is what turns "first empty slot" into a proof of absence for the lookup.
[[nodiscard]] constexpr std::size_t NextSlot(std::size_t slot) noexcept {
  return (slot + 1) & (kIndexSlots - 1);
}

}  // namespace BakeCache

#endif  // BAKE_HASH_H
