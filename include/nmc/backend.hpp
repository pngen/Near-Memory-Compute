#pragma once
// Execution backend interface plus the built-in backends:
//   * SYNTHETIC target backend  (deterministic, models near-memory semantics, not hardware)
//   * REAL host-local backend   (real CPU operation over real host memory)
// Neither pretends to be physical near-memory hardware.

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "nmc/digest.hpp"
#include "nmc/enums.hpp"

namespace nmc {

struct BackendRequest {
  OperationClass op_class = OperationClass::SUM;
  DataType data_type = DataType::U8;
  std::span<const std::uint8_t> input;  // actual bytes to operate on
  std::uint64_t input_bytes = 0;        // declared size (must be <= input.size())
};

struct BackendResult {
  bool ok = false;
  std::vector<std::uint8_t> output;
  ContentDigest digest;
  std::uint64_t execution_ns = 0;
  std::string note;
};

// Reduce input bytes to a small deterministic result based on the operation class.
// This is the SYNTHETIC near-memory engine semantics; it is explicitly not hardware.
BackendResult synthetic_execute(const BackendRequest& req);

// Real host-local execution: operates on real host memory and verifies a CPU reference.
// This is REAL host-local execution, not near-memory hardware.
BackendResult host_local_execute(const BackendRequest& req);

}  // namespace nmc
