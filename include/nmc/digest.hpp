#pragma once
// Non-cryptographic content digest (FNV-1a 64) used for result/program verification. It is a
// correctness identity, not a security primitive. Deterministic and portable.

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <span>
#include <string>

namespace nmc {

struct ContentDigest {
  std::uint64_t value = 0;
  friend bool operator==(const ContentDigest& a, const ContentDigest& b) noexcept { return a.value == b.value; }
  friend bool operator!=(const ContentDigest& a, const ContentDigest& b) noexcept { return a.value != b.value; }
};

inline ContentDigest content_digest(std::span<const std::uint8_t> data) noexcept {
  std::uint64_t h = 0xcbf29ce484222325ull;
  for (auto b : data) {
    h ^= static_cast<std::uint64_t>(b);
    h *= 0x100000001b3ull;
  }
  return ContentDigest{h};
}

inline std::string to_hex(ContentDigest d) {
  std::ostringstream oss;
  oss << std::hex << std::setw(16) << std::setfill('0') << d.value;
  return oss.str();
}

}  // namespace nmc
