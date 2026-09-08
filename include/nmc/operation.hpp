#pragma once
// Constrained operation model. Near-Memory Compute governs bounded, explicit operation
// classes; it is never "execute arbitrary code near memory".

#include <cstdint>
#include <optional>

#include "nmc/dimension.hpp"
#include "nmc/enums.hpp"
#include "nmc/ids.hpp"

namespace nmc {

// Result integrity requirement for an operation.
enum class IntegrityRequirement : int { NONE = 0, CHECKED = 1, STRICT = 2 };

inline const char* to_string(IntegrityRequirement v) {
  static constexpr const char* m[] = {"NONE","CHECKED","STRICT"};
  return (static_cast<int>(v) >= 0 && static_cast<int>(v) < 3) ? m[static_cast<int>(v)] : "INVALID_INTEGRITY_REQUIREMENT";
}

// An explicit, bounded operation. All fields needed to evaluate safety and economics.
struct OperationSpec {
  OperationId id;
  OperationGeneration generation;
  OperationClass op_class = OperationClass::SUM;
  DatasetId dataset;
  DatasetGeneration dataset_generation;
  DataRegionId region;
  DataRegionGeneration region_generation;
  DataShape shape;
  std::uint64_t input_bytes = 0;
  std::uint64_t output_bytes = 0;
  std::uint64_t workspace_bytes = 0;
  DataType output_data_type = DataType::F64;
  bool determinism_required = true;
  std::uint64_t max_tolerated_latency_ns = 0;   // 0 => no explicit bound
  std::uint64_t required_throughput_bps = 0;    // 0 => no explicit bound
  IntegrityRequirement integrity = IntegrityRequirement::CHECKED;
  RetryClass retry_class = RetryClass::NON_RETRYABLE;  // default conservative
  PolicyId policy;
  PolicyGeneration policy_generation;
  std::optional<KernelOrProgramId> program;     // if a versioned program is required
  std::optional<ProgramGeneration> program_generation;
  std::string description;
};

template <>
inline std::optional<IntegrityRequirement> parse_enum<IntegrityRequirement>(std::string_view n) {
  return parse_range<IntegrityRequirement>(3, n);
}

}  // namespace nmc
