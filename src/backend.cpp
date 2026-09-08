#include "nmc/backend.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace nmc {

namespace {
std::uint8_t byte_min(const std::uint8_t* p, std::size_t n) {
  std::uint8_t m = 0xFF; for (std::size_t i = 0; i < n; ++i) m = std::min(m, p[i]); return m;
}
std::uint8_t byte_max(const std::uint8_t* p, std::size_t n) {
  std::uint8_t m = 0; for (std::size_t i = 0; i < n; ++i) m = std::max(m, p[i]); return m;
}
std::uint64_t byte_sum(const std::uint8_t* p, std::size_t n) {
  std::uint64_t s = 0; for (std::size_t i = 0; i < n; ++i) s += p[i]; return s;
}
}  // namespace

BackendResult synthetic_execute(const BackendRequest& req) {
  BackendResult r;
  const std::uint8_t* p = req.input.data();
  std::size_t n = static_cast<std::size_t>(req.input_bytes);
  if ((!p && n != 0) || (req.input_bytes != req.input.size())) { r.note = "input size mismatch"; return r; }
  std::size_t n0 = n;
  switch (req.op_class) {
    case OperationClass::SUM: case OperationClass::REDUCE: {
      std::uint64_t s = byte_sum(p, n);
      // 8-byte little-endian result.
      r.output.resize(8);
      for (int i = 0; i < 8; ++i) r.output[i] = static_cast<std::uint8_t>((s >> (i * 8)) & 0xFF);
      break;
    }
    case OperationClass::COUNT: {
      std::uint64_t c = n;
      r.output.resize(8);
      for (int i = 0; i < 8; ++i) r.output[i] = static_cast<std::uint8_t>((c >> (i * 8)) & 0xFF);
      break;
    }
    case OperationClass::MIN: { auto v = byte_min(p, n); r.output.assign(1, v); break; }
    case OperationClass::MAX: { auto v = byte_max(p, n); r.output.assign(1, v); break; }
    case OperationClass::CHECKSUM: {
      std::uint64_t s = byte_sum(p, n);
      r.output.resize(8);
      for (int i = 0; i < 8; ++i) r.output[i] = static_cast<std::uint8_t>((s >> (i * 8)) & 0xFF);
      break;
    }
    case OperationClass::HASH: {
      // Digest of the input as the result bytes.
      std::uint64_t h = content_digest(req.input).value;
      r.output.resize(8);
      for (int i = 0; i < 8; ++i) r.output[i] = static_cast<std::uint8_t>((h >> (i * 8)) & 0xFF);
      break;
    }
    case OperationClass::FILTER: {
      std::uint64_t cnt = 0;
      for (std::size_t i = 0; i < n; ++i) if (p[i] >= 128) ++cnt;
      r.output.resize(8);
      for (int i = 0; i < 8; ++i) r.output[i] = static_cast<std::uint8_t>((cnt >> (i * 8)) & 0xFF);
      break;
    }
    case OperationClass::ELEMENTWISE: case OperationClass::TRANSFORM: {
      r.output.resize(n);
      for (std::size_t i = 0; i < n; ++i) r.output[i] = p[i];
      break;
    }
    default: {
      r.note = "operation not supported by synthetic engine";
      return r;
    }
  }
  r.digest = content_digest(std::span<const std::uint8_t>(r.output.data(), r.output.size()));
  r.ok = true;
  r.note = (n0 == 0 ? "empty input" : "synthetic result");
  return r;
}

BackendResult host_local_execute(const BackendRequest& req) {
  // Real host-local reduction over real host memory, with a CPU reference path. This is REAL
  // computation, clearly labeled host-local.
  BackendResult r;
  const std::uint8_t* p = req.input.data();
  std::size_t n = static_cast<std::size_t>(req.input_bytes);
  if ((!p && n != 0) || (req.input_bytes != req.input.size())) { r.note = "input size mismatch"; return r; }
  auto t0 = std::chrono::steady_clock::now();
  switch (req.op_class) {
    case OperationClass::SUM: case OperationClass::REDUCE: {
      std::uint64_t s = byte_sum(p, n);
      r.output.resize(8);
      for (int i = 0; i < 8; ++i) r.output[i] = static_cast<std::uint8_t>((s >> (i * 8)) & 0xFF);
      break;
    }
    case OperationClass::COUNT: {
      std::uint64_t c = n;
      r.output.resize(8);
      for (int i = 0; i < 8; ++i) r.output[i] = static_cast<std::uint8_t>((c >> (i * 8)) & 0xFF);
      break;
    }
    case OperationClass::MIN: { auto v = byte_min(p, n); r.output.assign(1, v); break; }
    case OperationClass::MAX: { auto v = byte_max(p, n); r.output.assign(1, v); break; }
    case OperationClass::FILTER: {
      std::uint64_t cnt = 0;
      for (std::size_t i = 0; i < n; ++i) if (p[i] >= 128) ++cnt;
      r.output.resize(8);
      for (int i = 0; i < 8; ++i) r.output[i] = static_cast<std::uint8_t>((cnt >> (i * 8)) & 0xFF);
      break;
    }
    default: {
      r.note = "operation not supported by host-local engine";
      return r;
    }
  }
  auto t1 = std::chrono::steady_clock::now();
  r.execution_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
  r.digest = content_digest(std::span<const std::uint8_t>(r.output.data(), r.output.size()));
  r.ok = true;
  r.note = "real host-local result";
  return r;
}

}  // namespace nmc
