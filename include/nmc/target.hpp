#pragma once
// Near-memory target model. Static/durable facts (definition, compatibility) are separated
// from dynamic observations (readiness, health, performance, ownership). A target that is
// physically near memory is not by itself beneficial; an enum value is not proof of hardware.

#include <cstdint>
#include <vector>

#include "nmc/enums.hpp"
#include "nmc/ids.hpp"

namespace nmc {

// Coherency/direct-access semantics for a target.
enum class AccessSemantics : int { COHERENT = 0, NON_COHERENT = 1, DIRECT = 2, STAGED = 3 };

inline const char* to_string(AccessSemantics v) {
  static constexpr const char* m[] = {"COHERENT","NON_COHERENT","DIRECT","STAGED"};
  return (static_cast<int>(v) >= 0 && static_cast<int>(v) < 4) ? m[static_cast<int>(v)] : "INVALID_ACCESS_SEMANTICS";
}

// A performance estimate. If measured is false, it is an assumption and must not be
// presented as measured evidence.
struct PerfEstimate {
  std::uint64_t latency_ns = 0;
  std::uint64_t throughput_bps = 0;   // bytes/second aggregate
  std::uint64_t setup_ns = 0;
  double confidence = 0.0;             // [0,1]
  bool measured = false;
};

// Durable static target definition. This survives restart and is revalidated as a whole.
struct TargetDefinition {
  NearMemoryTargetId id;
  NearMemoryTargetGeneration generation;
  ProviderKind provider = ProviderKind::UNKNOWN;
  TargetKind kind = TargetKind::UNKNOWN;
  std::string name;
  std::string description;
  FailureDomainId failure_domain;
  std::vector<MemoryDomainId> memory_domains;      // domains this target is bound to
  std::vector<OperationClass> supported_ops;        // statically advertised ops
  std::vector<DataType> supported_data_types;
  std::vector<Layout> supported_layouts;
  std::uint64_t max_input_bytes = 0;                // 0 means unbounded within budget
  std::uint64_t alignment_constraint = 1;
  std::uint32_t concurrency_capacity = 1;           // admission slots
  std::uint64_t queue_depth = 1;
  std::uint64_t workspace_bytes = 0;                // temporary workspace budget
  AccessSemantics access_semantics = AccessSemantics::DIRECT;
  bool requires_staging = false;
  bool synthetic = true;                            // true => SYNTHETIC, never REAL hardware
  bool supports_verification = false;
  bool supports_cancellation = false;
  bool supports_drain = false;
  std::string authority_owner;                      // "coordinator"/"worker" binding hint

  bool is_synthetic() const noexcept { return synthetic || kind == TargetKind::SYNTHETIC; }
};

// Dynamic target runtime state. On restart/worker-death it must transition to
// REVALIDATION_REQUIRED rather than remain authoritative.
struct TargetRuntime {
  TargetLifecycle lifecycle = TargetLifecycle::DISCOVERED;
  NearMemoryTargetId id;
  WorkerBootId owner_boot;                // worker incarnation that owns dynamic evidence
  EvidenceId evidence_id;
  EvidenceGeneration evidence_generation;
  bool reachable = false;
  std::uint64_t concurrency_in_use = 0;
  std::uint64_t workspace_in_use = 0;
  std::uint64_t current_queue_depth = 0;
  PerfEstimate performance;
  bool dynamic_current = false;           // false => REVALIDATION_REQUIRED
  std::uint32_t queued = 0;
};

inline const char* classification_label(const TargetDefinition& def, const TargetRuntime& run) {
  if (def.synthetic) return "SYNTHETIC";
  if (run.reachable && run.dynamic_current) return "REAL";
  if (def.kind == TargetKind::HOST_CPU_LOCAL) return "REAL";
  return "UNKNOWN";
}

template <>
inline std::optional<AccessSemantics> parse_enum<AccessSemantics>(std::string_view n) {
  return parse_range<AccessSemantics>(4, n);
}

}  // namespace nmc
