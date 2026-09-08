#pragma once
// Small, dependency-free helpers. Overflow-checked arithmetic is used on every size/count
// computation that crosses a trust boundary (persistence, protocol, planning).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace nmc {

inline std::optional<std::uint64_t> checked_add_u64(std::uint64_t a, std::uint64_t b) noexcept {
  if (b > UINT64_MAX - a) return std::nullopt;
  return a + b;
}

inline std::optional<std::uint64_t> checked_mul_u64(std::uint64_t a, std::uint64_t b) noexcept {
  if (a != 0 && b > UINT64_MAX / a) return std::nullopt;
  return a * b;
}

// Round value up to a power-of-two alignment. Returns 0 on overflow.
inline std::uint64_t align_up_u64(std::uint64_t value, std::uint64_t alignment) noexcept {
  if (alignment == 0) return value;
  std::uint64_t rem = value % alignment;
  if (rem == 0) return value;
  std::uint64_t add = alignment - rem;
  std::uint64_t result = value + add;
  if (result < value) return 0;  // overflow
  return result;
}

inline bool is_power_of_two(std::uint64_t v) noexcept { return v != 0 && (v & (v - 1)) == 0; }

}  // namespace nmc
