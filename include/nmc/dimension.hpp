#pragma once
// Data dimension/layout descriptors plus trusted size arithmetic.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "nmc/enums.hpp"
#include "nmc/util.hpp"

namespace nmc {

// Bytes per element for a DataType. Returns 0 for an unrecognized value.
inline std::uint64_t bytes_per_element(DataType dt) noexcept {
  switch (dt) {
    case DataType::U8: case DataType::I8: return 1;
    case DataType::U16: case DataType::I16: case DataType::F16: case DataType::BF16: return 2;
    case DataType::U32: case DataType::I32: case DataType::F32: return 4;
    case DataType::U64: case DataType::I64: case DataType::F64: return 8;
    default: return 0;
  }
}

using Bytes = std::uint64_t;

// A bounded data descriptor: element count, type, and layout.
struct DataShape {
  std::uint64_t element_count = 0;  // 0 is only valid for an empty region
  DataType data_type = DataType::U8;
  Layout layout = Layout::CONTIGUOUS;
  std::uint64_t alignment = 1;  // required natural alignment in bytes

  // Total contiguous element bytes with overflow checking. Returns nullopt on overflow.
  std::optional<std::uint64_t> element_bytes() const noexcept {
    auto n = bytes_per_element(data_type);
    if (n == 0) return std::nullopt;
    return checked_mul_u64(element_count, n);
  }
};

// A single contiguous byte window with byte offset/length, bounds-checked.
// This value is cheap; it does not own the bytes it references.
struct ByteWindow {
  std::uint64_t offset = 0;
  std::uint64_t length = 0;

  friend bool operator==(const ByteWindow& a, const ByteWindow& b) noexcept {
    return a.offset == b.offset && a.length == b.length;
  }
  friend bool operator!=(const ByteWindow& a, const ByteWindow& b) noexcept { return !(a == b); }
};

}  // namespace nmc
