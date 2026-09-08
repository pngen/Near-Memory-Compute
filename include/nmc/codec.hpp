#pragma once
// Bounded binary codec. All reads validate bounds BEFORE accessing memory; reads never use
// &buffer[pos] when pos may equal buffer.size(). Every length is checked, and counts are
// bounded so a malicious or corrupt frame cannot drive an enormous allocation.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "nmc/util.hpp"

namespace nmc {

// Hard bound on a single blobs/string length accepted by the reader to bound allocations.
constexpr std::uint64_t kMaxCodecBlobBytes = 64ull * 1024 * 1024;  // 64 MiB

class ByteReader {
 public:
  ByteReader() = default;
  ByteReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

  bool valid() const noexcept { return data_ != nullptr || size_ == 0; }
  std::size_t remaining() const noexcept { return size_ > pos_ ? size_ - pos_ : 0; }
  bool at_end() const noexcept { return pos_ >= size_; }
  std::size_t position() const noexcept { return pos_; }

  bool read_u8(std::uint8_t& v) noexcept { return read_fixed(&v, 1); }
  bool read_u16(std::uint16_t& v) noexcept { return read_fixed(&v, 2); }
  bool read_u32(std::uint32_t& v) noexcept { return read_fixed(&v, 4); }
  bool read_u64(std::uint64_t& v) noexcept { return read_fixed(&v, 8); }
  bool read_i64(std::int64_t& v) noexcept { return read_fixed(&v, 8); }
  bool read_f64(double& v) noexcept { return read_fixed(&v, 8); }

  // Read n bytes into an opaque span over the source buffer (no copy).
  bool read_span(std::span<const std::uint8_t>& span, std::size_t n) noexcept {
    auto ov = checked_mul_u64(n, 1);
    if (!ov) return false;  // overflow
    if (n > remaining()) return false;
    // n <= remaining(), so pos_ is in range and pos_+n <= size_.
    span = std::span<const std::uint8_t>(data_ + pos_, n);
    pos_ += n;
    return true;
  }

  // Read a length-prefixed byte blob. Rejects lengths beyond kMaxCodecBlobBytes.
  bool read_blob(std::vector<std::uint8_t>& out) noexcept {
    std::uint64_t len = 0;
    if (!read_u64(len)) return false;
    if (len > kMaxCodecBlobBytes) return false;
    if (len > remaining()) return false;
    out.resize(static_cast<std::size_t>(len));
    if (len > 0) {
      std::memcpy(out.data(), data_ + pos_, static_cast<std::size_t>(len));
      pos_ += static_cast<std::size_t>(len);
    }
    return true;
  }

  // Read a length-prefixed string (UTF-8). Bounded like blobs.
  bool read_string(std::string& out) noexcept {
    std::uint64_t len = 0;
    if (!read_u64(len)) return false;
    if (len > kMaxCodecBlobBytes) return false;
    if (len > remaining()) return false;
    out.assign(reinterpret_cast<const char*>(data_ + pos_), static_cast<std::size_t>(len));
    pos_ += static_cast<std::size_t>(len);
    return true;
  }

  // Skip n bytes.
  bool skip(std::size_t n) noexcept {
    if (n > remaining()) return false;
    pos_ += n;
    return true;
  }

 private:
  bool read_fixed(void* dst, std::size_t n) noexcept {
    if (n > remaining()) return false;
    std::memcpy(dst, data_ + pos_, n);
    pos_ += n;
    return true;
  }

  const std::uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t pos_ = 0;
};

class ByteWriter {
 public:
  void write_u8(std::uint8_t v) noexcept { buf_.push_back(v); }
  void write_u16(std::uint16_t v) noexcept { append(&v, 2); }
  void write_u32(std::uint32_t v) noexcept { append(&v, 4); }
  void write_u64(std::uint64_t v) noexcept { append(&v, 8); }
  void write_i64(std::int64_t v) noexcept { append(&v, 8); }
  void write_f64(double v) noexcept { append(&v, 8); }

  void write_blob(const std::uint8_t* data, std::size_t n) noexcept {
    write_u64(n);
    if (n > 0) append(data, n);
  }
  // Raw append with no length prefix.
  void write_raw(const std::uint8_t* data, std::size_t n) noexcept {
    if (n > 0) append(data, n);
  }
  void write_string(std::string_view s) noexcept { write_blob(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); }

  std::vector<std::uint8_t> take() noexcept { return std::move(buf_); }
  const std::vector<std::uint8_t>& buffer() const noexcept { return buf_; }
  std::size_t size() const noexcept { return buf_.size(); }

 private:
  void append(const void* data, std::size_t n) noexcept {
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
    buf_.insert(buf_.end(), p, p + n);
  }
  std::vector<std::uint8_t> buf_;
};

}  // namespace nmc
