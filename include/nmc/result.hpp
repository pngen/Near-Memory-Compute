#pragma once
// Result model with full provenance. A completion must not become authoritative until
// verification passes and authority is validated.

#include <cstdint>
#include <string>

#include "nmc/enums.hpp"
#include "nmc/ids.hpp"

namespace nmc {

struct ExecutionResult {
  ResultId id;
  ResultGeneration generation;
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
  OperationGeneration operation_generation;
  VerifyState verification = VerifyState::UNVERIFIED;
  std::string output_digest;   // content digest of the produced result, when verified
  std::uint64_t output_bytes = 0;
  bool committed = false;      // only true once accepted by the coordinator
  bool ambiguous = false;      // true when OUTCOME_UNKNOWN
  std::string note;
};

}  // namespace nmc
