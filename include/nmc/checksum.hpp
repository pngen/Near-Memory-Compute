#pragma once
// Integrity checksum (CRC-32 / IEEE) used for persistence and protocol framing.
// Deterministic, portable, and fast enough for hot-path provenance checks.

#include <array>
#include <cstddef>
#include <cstdint>

namespace nmc {

class Crc32 {
 public:
  Crc32() noexcept = default;
  void update(const void* data, std::size_t n) noexcept {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < n; ++i) {
      value_ = (value_ >> 8) ^ table()[(value_ ^ p[i]) & 0xFFu];
    }
  }
  template <class T>
  void update_val(const T& v) noexcept {
    const unsigned char* raw = reinterpret_cast<const unsigned char*>(&v);
    update(raw, sizeof(T));
  }
  void update_bytes(std::uint64_t v) noexcept { update_val<std::uint64_t>(v); }
  void update_u16(std::uint16_t v) noexcept { update_val<std::uint16_t>(v); }
  void update_u32(std::uint32_t v) noexcept { update_val<std::uint32_t>(v); }
  void update_u64(std::uint64_t v) noexcept { update_val<std::uint64_t>(v); }
  std::uint32_t digest() const noexcept { return value_; }
  void reset() noexcept { value_ = 0xFFFFFFFFu; }

 private:
  static const std::array<std::uint32_t, 256>& table() noexcept {
    static const std::array<std::uint32_t, 256> t = [] {
      std::array<std::uint32_t, 256> tab{};
      std::uint32_t pol = 0xEDB88320u;
      for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (pol ^ (c >> 1)) : (c >> 1);
        tab[i] = c;
      }
      return tab;
    }();
    return t;
  }
  std::uint32_t value_ = 0xFFFFFFFFu;
};

inline std::uint32_t crc32(const void* data, std::size_t n) noexcept {
  Crc32 c; c.update(data, n); return c.digest();
}

}  // namespace nmc
