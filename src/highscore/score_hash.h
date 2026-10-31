#ifndef SCORE_HASH_H
#define SCORE_HASH_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

/** @namespace ScoreHash @brief Tamper-evident digest for the persisted Hall of Fame ledger. Why a
 * dedicated header (AGENT 1 / AGENT 5): - The score file lives in user-writable storage ... */
namespace ScoreHash {

/// Hex characters kept from the 256-bit digest (8 bytes = 64 bits): enough to make offline forgery
/// impractical, short enough to keep the ledger human-readable.
inline constexpr std::size_t kDigestHexChars = 16;

/// Compile-time salt mixed into every digest. Deliberately part of the
/// executable's .rodata, never of the data file it protects.
inline constexpr std::string_view kSalt = "AliensInvaders::HallOfFame::v1";

namespace detail {

/// SHA-256 round constants (FIPS 180-4: first 32 bits of the fractional parts of the cube roots of
/// the first 64 primes). constexpr, so the table lands in .rodata (AGENT 4).
inline constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506ceb, 0xbef9a3f7u, 0xc67178f2u};

/// 32-bit rotate right -- the only bit maneuver SHA-256 needs.
[[nodiscard]] inline constexpr std::uint32_t Rotr(std::uint32_t x, int n) noexcept {
  return (x >> n) | (x << (32 - n));
}

}  // namespace detail

/// Full 32-byte SHA-256 (FIPS 180-4) of `msg`. One small padded-buffer allocation per call, on the
/// save/load path only -- never per frame. Exposed for the known-answer unit test; row signing ...
[[nodiscard]] inline std::array<std::uint8_t, 32> Sha256(std::string_view msg) {
  std::array<std::uint32_t, 8> h{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

  // Standard FIPS 180-4 padding: msg || 0x80 || zero fill || 64-bit
  // big-endian bit length, rounded up to whole 64-byte blocks.
  const std::uint64_t bit_len = static_cast<std::uint64_t>(msg.size()) * 8u;
  std::string padded(msg);
  padded.push_back(static_cast<char>(0x80));
  while (padded.size() % 64U != 56U) padded.push_back('\0');
  for (int shift = 56; shift >= 0; shift -= 8) {
    padded.push_back(static_cast<char>((bit_len >> shift) & 0xFFu));
  }

  std::array<std::uint32_t, 64> w{};
  for (std::size_t block = 0; block < padded.size(); block += 64U) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(padded.data() + block);
    for (std::size_t i = 0; i < 16U; ++i) {
      w[i] = (static_cast<std::uint32_t>(p[i * 4U]) << 24) |
             (static_cast<std::uint32_t>(p[i * 4U + 1U]) << 16) |
             (static_cast<std::uint32_t>(p[i * 4U + 2U]) << 8) |
             static_cast<std::uint32_t>(p[i * 4U + 3U]);
    }
    for (std::size_t i = 16U; i < 64U; ++i) {
      const std::uint32_t s0 =
          detail::Rotr(w[i - 15U], 7) ^ detail::Rotr(w[i - 15U], 18) ^ (w[i - 15U] >> 3);
      const std::uint32_t s1 =
          detail::Rotr(w[i - 2U], 17) ^ detail::Rotr(w[i - 2U], 19) ^ (w[i - 2U] >> 10);
      w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }

    std::array<std::uint32_t, 8> v = h;
    for (std::size_t i = 0; i < 64U; ++i) {
      const std::uint32_t big_s1 =
          detail::Rotr(v[4], 6) ^ detail::Rotr(v[4], 11) ^ detail::Rotr(v[4], 25);
      const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
      const std::uint32_t t1 = v[7] + big_s1 + ch + detail::kRoundConstants[i] + w[i];
      const std::uint32_t big_s0 =
          detail::Rotr(v[0], 2) ^ detail::Rotr(v[0], 13) ^ detail::Rotr(v[0], 22);
      const std::uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
      const std::uint32_t t2 = big_s0 + maj;
      v[7] = v[6];
      v[6] = v[5];
      v[5] = v[4];
      v[4] = v[3] + t1;
      v[3] = v[2];
      v[2] = v[1];
      v[1] = v[0];
      v[0] = t1 + t2;
    }
    for (std::size_t i = 0; i < 8U; ++i) h[i] += v[i];
  }

  std::array<std::uint8_t, 32> out{};
  for (std::size_t i = 0; i < 8U; ++i) {
    out[i * 4U] = static_cast<std::uint8_t>(h[i] >> 24);
    out[i * 4U + 1U] = static_cast<std::uint8_t>(h[i] >> 16);
    out[i * 4U + 2U] = static_cast<std::uint8_t>(h[i] >> 8);
    out[i * 4U + 3U] = static_cast<std::uint8_t>(h[i]);
  }
  return out;
}

/// Digest column value for one ledger row: the first kDigestHexChars lowercase hex characters of
/// SHA-256(kSalt || payload), where payload is the row exactly as serialized, minus its ...
[[nodiscard]] inline std::string LineDigest(std::string_view payload) {
  std::string salted(kSalt);
  salted += payload;
  const std::array<std::uint8_t, 32> digest = Sha256(salted);

  static constexpr char kHexDigits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(kDigestHexChars);
  for (std::size_t i = 0; i < kDigestHexChars / 2U; ++i) {
    hex.push_back(kHexDigits[(digest[i] >> 4) & 0x0Fu]);
    hex.push_back(kHexDigits[digest[i] & 0x0Fu]);
  }
  return hex;
}

/// True when `digest` authenticates `payload`. A plain equality compare is enough: the threat model
/// is offline file editing between sessions, not a timing side channel against a live network ...
[[nodiscard]] inline bool VerifyLine(std::string_view payload, std::string_view digest) {
  return digest.size() == kDigestHexChars && digest == LineDigest(payload);
}

}  // namespace ScoreHash

#endif  // SCORE_HASH_H
