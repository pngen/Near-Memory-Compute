#pragma once
// Hard feasibility filtering (which MUST precede economics) plus deterministic ranking.

#include <algorithm>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "nmc/authority.hpp"
#include "nmc/capability.hpp"
#include "nmc/dataset.hpp"
#include "nmc/economics.hpp"
#include "nmc/ids.hpp"
#include "nmc/operation.hpp"
#include "nmc/policy.hpp"
#include "nmc/program.hpp"
#include "nmc/target.hpp"

namespace nmc {

struct EligibilityReasonDetail {
  EligibilityReason reason = EligibilityReason::NONE;
  std::string detail;
};

// Inputs needed for a single candidate hard-filter.
struct HardFilterInput {
  const TargetDefinition* target = nullptr;
  const TargetRuntime* runtime = nullptr;
  const OperationSpec* op = nullptr;
  const DataEvidence* data = nullptr;
  const CapabilitySet* caps = nullptr;
  const Program* program = nullptr;
  const Policy* policy = nullptr;
  const WorkerBootId* current_worker_boot = nullptr;
  const NearMemoryTargetGeneration* current_target_generation = nullptr;
};

// Workspace required by an operation.
inline std::uint64_t op_workspace_required(const OperationSpec& op) { return op.workspace_bytes; }

// Hard feasibility check. Returns empty vector if eligible; otherwise the explicit reasons.
inline std::vector<EligibilityReasonDetail> hard_filter(const HardFilterInput& in) {
  std::vector<EligibilityReasonDetail> reasons;
  if (in.target == nullptr || in.runtime == nullptr || in.op == nullptr || in.data == nullptr ||
      in.policy == nullptr || in.caps == nullptr) {
    reasons.push_back({EligibilityReason::TARGET_OFFLINE, "missing required evidence (null input)"});
    return reasons;
  }
  const auto& runtime = *in.runtime;
  const auto& target = *in.target;

  if (runtime.lifecycle == TargetLifecycle::OFFLINE || runtime.lifecycle == TargetLifecycle::FAILED ||
      runtime.lifecycle == TargetLifecycle::RETIRED)
    reasons.push_back({EligibilityReason::TARGET_OFFLINE, "target lifecycle " + std::string(to_string(runtime.lifecycle))});
  if (runtime.lifecycle == TargetLifecycle::DEGRADED)
    reasons.push_back({EligibilityReason::TARGET_DEGRADED, "target degraded"});
  if (!runtime.reachable)
    reasons.push_back({EligibilityReason::TARGET_UNREACHABLE, "target reported unreachable"});
  if (!runtime.dynamic_current || runtime.lifecycle == TargetLifecycle::REVALIDATION_REQUIRED)
    reasons.push_back({EligibilityReason::TARGET_REVALIDATION_REQUIRED, "dynamic target evidence requires revalidation"});

  // Operation support.
  const bool op_supported =
      std::find(target.supported_ops.begin(), target.supported_ops.end(), in.op->op_class) != target.supported_ops.end();
  if (!op_supported) reasons.push_back({EligibilityReason::OPERATION_UNSUPPORTED, "operation class not supported by target"});
  const auto op_cap_state = in.caps->state_of(std::string("operation:") + to_string(in.op->op_class));
  if (op_cap_state == CapabilityState::UNKNOWN)
    reasons.push_back({EligibilityReason::OPERATION_UNKNOWN, "operation capability UNKNOWN (fails closed)"});
  else if (op_cap_state == CapabilityState::REVALIDATION_REQUIRED)
    reasons.push_back({EligibilityReason::TARGET_REVALIDATION_REQUIRED, "operation capability requires revalidation"});

  if (std::find(target.supported_data_types.begin(), target.supported_data_types.end(), in.op->shape.data_type) ==
      target.supported_data_types.end())
    reasons.push_back({EligibilityReason::DATATYPE_UNSUPPORTED, "data type not supported"});
  if (std::find(target.supported_layouts.begin(), target.supported_layouts.end(), in.op->shape.layout) ==
      target.supported_layouts.end())
    reasons.push_back({EligibilityReason::LAYOUT_UNSUPPORTED, "layout not supported"});
  if (target.alignment_constraint != 0 && in.op->shape.alignment % target.alignment_constraint != 0)
    reasons.push_back({EligibilityReason::ALIGNMENT_UNSUPPORTED, "data alignment violates target constraint"});

  // Data currency and reachability.
  if (!in.data->present) reasons.push_back({EligibilityReason::DATA_UNREACHABLE, "data not present in domain"});
  if (!in.data->current) reasons.push_back({EligibilityReason::DATA_STALE, "data not current"});
  if (in.data->dataset_generation != in.op->dataset_generation)
    reasons.push_back({EligibilityReason::DATA_WRONG_GENERATION, "data generation does not match operation"});
  if (!in.data->reachable) reasons.push_back({EligibilityReason::DATA_UNREACHABLE, "data not reachable from target"});

  // Workspace / concurrency.
  if (op_workspace_required(*in.op) > target.workspace_bytes)
    reasons.push_back({EligibilityReason::INSUFFICIENT_WORKSPACE, "workspace requirement exceeds target budget"});
  if (runtime.concurrency_in_use >= target.concurrency_capacity)
    reasons.push_back({EligibilityReason::CONCURRENCY_EXHAUSTED, "target concurrency capacity exhausted"});

  // Program compatibility.
  if (in.op->program.has_value()) {
    if (in.program == nullptr || in.program->id != in.op->program.value() ||
        in.program->lifecycle != ProgramLifecycle::READY)
      reasons.push_back({EligibilityReason::PROGRAM_INCOMPATIBLE, "program not READY or not registered"});
    if (in.program != nullptr && in.program->op_class != in.op->op_class)
      reasons.push_back({EligibilityReason::PROGRAM_INCOMPATIBLE, "program operation class mismatch"});
    if (in.program != nullptr && std::find(in.program->data_types.begin(), in.program->data_types.end(), in.op->shape.data_type) == in.program->data_types.end())
      reasons.push_back({EligibilityReason::PROGRAM_INCOMPATIBLE, "program data type mismatch"});
  }

  // Authority freshness.
  if (in.current_target_generation != nullptr && target.generation != *in.current_target_generation)
    reasons.push_back({EligibilityReason::WRONG_TARGET_GENERATION, "target generation is stale"});
  if (in.current_worker_boot != nullptr && runtime.owner_boot != *in.current_worker_boot)
    reasons.push_back({EligibilityReason::WRONG_WORKER_BOOT, "target owned by a different worker boot"});
  if (!in.caps->dynamic_current) reasons.push_back({EligibilityReason::EVIDENCE_STALE, "capability evidence stale"});
  if (runtime.performance.confidence < in.policy->required_confidence)
    reasons.push_back({EligibilityReason::INSUFFICIENT_CONFIDENCE, "performance confidence below policy threshold"});

  return reasons;
}

inline bool operation_supported(const TargetDefinition& t, OperationClass op) {
  return std::find(t.supported_ops.begin(), t.supported_ops.end(), op) != t.supported_ops.end();
}

// A ranked candidate after hard filtering.
struct RankedCandidate {
  NearMemoryTargetId target;
  NearMemoryTargetGeneration generation;
  Decision decision = Decision::NEAR_MEMORY_SELECTED;
  std::uint64_t movement_avoided_bytes = 0;
  std::uint64_t total_cost_ns = 0;
  std::uint64_t estimated_latency_ns = 0;
  double confidence = 0.0;
  std::vector<std::string> factors;
};

// Deterministic ranking: a strict total order (score, movement, latency, confidence, id) so the
// result is independent of insertion order. Ties are broken by identity, never by input order.
inline std::vector<RankedCandidate> rank_candidates(std::vector<RankedCandidate> candidates) {
  std::sort(candidates.begin(), candidates.end(), [](const RankedCandidate& a, const RankedCandidate& b) {
    if (a.total_cost_ns != b.total_cost_ns) return a.total_cost_ns < b.total_cost_ns;
    if (a.movement_avoided_bytes != b.movement_avoided_bytes) return a.movement_avoided_bytes > b.movement_avoided_bytes;
    if (a.estimated_latency_ns != b.estimated_latency_ns) return a.estimated_latency_ns < b.estimated_latency_ns;
    if (a.confidence != b.confidence) return a.confidence > b.confidence;
    return a.target.value() < b.target.value();  // final deterministic tie-break
  });
  return candidates;
}

}  // namespace nmc
