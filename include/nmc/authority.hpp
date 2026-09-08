#pragma once
// Authority model. Every plan binds an authority snapshot; every dispatch/completion must
// match the current authoritative generation set or be rejected BEFORE mutation. Stale
// traffic must never mutate current state.

#include <optional>
#include <string>
#include <vector>

#include "nmc/ids.hpp"

namespace nmc {

// The generation/identity subset that a plan pins, and that completion must reproduce.
struct AuthoritySnapshot {
  CoordinatorEpoch epoch;
  WorkerBootId worker_boot;
  NearMemoryTargetId target;
  NearMemoryTargetGeneration target_generation;
  MemoryDomainGeneration memory_domain_generation;
  DatasetGeneration dataset_generation;
  DataRegionGeneration region_generation;
  OperationGeneration operation_generation;
  std::optional<ProgramGeneration> program_generation;
  CapabilityGeneration capability_generation;
  PolicyGeneration policy_generation;
  EvidenceGeneration evidence_generation;
  ExecutionPlanGeneration plan_generation;
};

// The authority carried by an execution plan (target + data binding).
struct PlanAuthority {
  AuthoritySnapshot snapshot;
  ExecutionPlanId plan;
  ExecutionPlanGeneration plan_generation;
};

// The authority carried by a completion/result publication.
struct CompletionAuthority {
  ExecutionPlanId plan;
  ExecutionPlanGeneration plan_generation;
  DispatchId dispatch;
  AttemptId attempt;
  NearMemoryTargetId target;
  NearMemoryTargetGeneration target_generation;
  WorkerBootId worker_boot;
  CoordinatorEpoch epoch;
  DatasetGeneration dataset_generation;
  DataRegionGeneration region_generation;
  ResultGeneration result_generation;
  OperationGeneration operation_generation;
};

// A human-inspectable reason describing one stale-authority dimension.
struct StaleReason {
  std::string dimension;   // e.g. "epoch", "worker_boot", "target_generation"
  std::string detail;
};

// Validate a completion against the plan's pinned authority. Returns a non-empty vector of
// stale reasons if ANY material dimension mismatches; an empty vector means accepted.
inline std::vector<StaleReason> validate_completion_against_plan(
    const CompletionAuthority& completion, const PlanAuthority& plan) {
  std::vector<StaleReason> reasons;
  const auto& s = plan.snapshot;

  if (completion.plan != plan.plan) reasons.push_back({"plan", "completion plan id mismatch"});
  if (completion.plan_generation != plan.plan_generation)
    reasons.push_back({"plan_generation", "completion plan generation mismatch"});
  if (completion.target_generation != s.target_generation)
    reasons.push_back({"target_generation", "completion target generation mismatch"});
  if (completion.worker_boot != s.worker_boot)
    reasons.push_back({"worker_boot", "completion worker boot mismatch"});
  if (completion.epoch != s.epoch) reasons.push_back({"epoch", "completion epoch mismatch"});
  if (completion.dataset_generation != s.dataset_generation)
    reasons.push_back({"dataset_generation", "completion dataset generation mismatch"});
  if (completion.region_generation != s.region_generation)
    reasons.push_back({"region_generation", "completion region generation mismatch"});
  if (completion.operation_generation != s.operation_generation)
    reasons.push_back({"operation_generation", "completion operation generation mismatch"});
  return reasons;
}

// Validate a plan's pinned snapshot against the current authoritative snapshot. A plan is
// stale if ANY material generation moved. Empty vector => plan remains authoritative.
inline std::vector<StaleReason> validate_plan_against_current(
    const PlanAuthority& plan, const AuthoritySnapshot& current) {
  std::vector<StaleReason> reasons;
  const auto& s = plan.snapshot;

  if (s.epoch != current.epoch) reasons.push_back({"epoch", "coordinator epoch moved"});
  if (s.worker_boot != current.worker_boot)
    reasons.push_back({"worker_boot", "worker boot changed"});
  if (s.target_generation != current.target_generation)
    reasons.push_back({"target_generation", "target generation moved"});
  if (s.memory_domain_generation != current.memory_domain_generation)
    reasons.push_back({"memory_domain_generation", "memory domain generation moved"});
  if (s.dataset_generation != current.dataset_generation)
    reasons.push_back({"dataset_generation", "dataset generation moved"});
  if (s.region_generation != current.region_generation)
    reasons.push_back({"region_generation", "region generation moved"});
  if (s.operation_generation != current.operation_generation)
    reasons.push_back({"operation_generation", "operation generation moved"});
  if (s.program_generation != current.program_generation)  // handles nullopt equality
    reasons.push_back({"program_generation", "program generation moved"});
  if (s.capability_generation != current.capability_generation)
    reasons.push_back({"capability_generation", "capability generation moved"});
  if (s.policy_generation != current.policy_generation)
    reasons.push_back({"policy_generation", "policy generation moved"});
  if (s.evidence_generation != current.evidence_generation)
    reasons.push_back({"evidence_generation", "evidence generation moved"});
  return reasons;
}

}  // namespace nmc
