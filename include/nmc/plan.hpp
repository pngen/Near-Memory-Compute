#pragma once
// Execution plan. A plan binds a complete authority snapshot and is generation-bound: it must
// fail before dispatch if any hard authority component changed.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nmc/authority.hpp"
#include "nmc/enums.hpp"
#include "nmc/ids.hpp"

namespace nmc {

struct ExecutionPlan {
  ExecutionPlanId id;
  ExecutionPlanGeneration generation;
  PlanAuthority authority;                  // pinned snapshot
  NearMemoryTargetId target;
  NearMemoryTargetGeneration target_generation;
  OperationId operation;
  OperationClass op_class = OperationClass::SUM;
  DatasetId dataset;
  DatasetGeneration dataset_generation;
  DataRegionId region;
  DataRegionGeneration region_generation;
  ExecutionLifecycle lifecycle = ExecutionLifecycle::PLANNED;
  std::uint64_t reserved_slots = 0;
  std::uint64_t reserved_workspace = 0;
  std::optional<DispatchId> dispatch_id;
  std::optional<AttemptId> attempt_id;
  RetryClass retry_class = RetryClass::NON_RETRYABLE;
  Decision decision = Decision::MOVEMENT_REQUIRED;
  std::vector<std::string> notes;
};

}  // namespace nmc
