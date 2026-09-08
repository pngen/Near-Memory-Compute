#pragma once
// The Coordinator is the near-memory control plane. It owns the decision and authority
// boundary for executing supported operations close to data-bearing memory domains. All
// engine methods are pure state mutations under one lock; NO socket I/O or backend execution
// happens while the state lock is held (that is the network/controller's responsibility).

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "nmc/authority.hpp"
#include "nmc/backend.hpp"
#include "nmc/capability.hpp"
#include "nmc/dataset.hpp"
#include "nmc/enums.hpp"
#include "nmc/expected.hpp"
#include "nmc/ids.hpp"
#include "nmc/operation.hpp"
#include "nmc/persistence.hpp"
#include "nmc/plan.hpp"
#include "nmc/planning.hpp"
#include "nmc/policy.hpp"
#include "nmc/program.hpp"
#include "nmc/protocol.hpp"
#include "nmc/result.hpp"
#include "nmc/target.hpp"

namespace nmc {

struct ReservationState {
  std::uint64_t reserved_slots = 0;
  std::uint64_t reserved_workspace = 0;
};

struct WorkerSession {
  WorkerId worker;
  WorkerBootId boot;
  std::string name;
  bool connected = false;
};

// Durable + dynamic runtime state. Dynamic fields are marked non-current on recovery.
struct RuntimeState {
  CoordinatorId coordinator_id;
  CoordinatorEpoch epoch;
  std::map<MemoryDomainId, MemoryDomainDescriptor> memory_domains;
  std::map<DatasetId, DatasetDescriptor> datasets;
  std::map<DataRegionId, DataRegionDescriptor> regions;
  std::map<DataRegionId, DataEvidence> data_evidence;
  std::map<NearMemoryTargetId, TargetDefinition> targets;
  std::map<NearMemoryTargetId, TargetRuntime> target_runtime;
  std::map<NearMemoryTargetId, CapabilitySet> capabilities;
  std::map<OperationId, OperationSpec> operations;
  std::map<KernelOrProgramId, Program> programs;
  std::map<PolicyId, Policy> policies;
  std::map<ExecutionPlanId, ExecutionPlan> plans;
  std::map<OperationId, ExecutionPlanId> active_plans;  // one active plan per operation (hot-path index)
  std::map<ResultId, ExecutionResult> results;
  std::map<NearMemoryTargetId, ReservationState> reservations;
  std::map<WorkerId, WorkerSession> workers;
};

class Coordinator {
 public:
  Coordinator();
  explicit Coordinator(CoordinatorId id);

  // ---- Static/durable registration. ----
  Result<void> attach_coordinator(CoordinatorId id);
  Result<void> register_memory_domain(const MemoryDomainDescriptor& md);
  Result<void> register_dataset(const DatasetDescriptor& ds);
  Result<void> register_region(const DataRegionDescriptor& rg);
  Result<void> register_program(const Program& pg);
  Result<void> register_policy(const Policy& pl);
  Result<void> create_operation(const OperationSpec& op);

  // ---- Dynamic publish from a worker. ----
  Result<void> apply_register_target(const RegisterTargetMsg& msg);
  Result<void> apply_capability(const CapabilityMsg& msg);
  Result<void> apply_data_evidence(const DataEvidenceMsg& msg);
  Result<void> apply_heartbeat(const HeartbeatMsg& msg);
  Result<void> bind_worker_session(const WorkerId& w, const WorkerBootId& boot, const std::string& name);

  // ---- Planning / reservation / dispatch. ----
  struct PlanOutput {
    ExecutionPlan plan;
    Decision decision = Decision::REJECT;
    std::vector<EligibilityReasonDetail> rejected;
    std::vector<RankedCandidate> ranked;
    std::vector<std::string> explanation;
    bool created = false;
  };
  Result<PlanOutput> plan(const OperationId& op_id);
  Result<void> reserve(const ExecutionPlanId& plan_id);

  struct DispatchOutput {
    bool ready = false;
    DispatchMsg message;
    WorkerId worker;
  };
  Result<DispatchOutput> prepare_dispatch(const ExecutionPlanId& plan_id, const DispatchId& dispatch_id,
                                          const AttemptId& attempt_id);
  Result<void> confirm_dispatched(const ExecutionPlanId& plan_id, const DispatchId& dispatch_id);

  // ---- Completion / verification. ----
  Result<ExecutionResult> apply_result(const ResultMsg& msg);

  // ---- Lifecycle / failure handling. ----
  Result<void> begin_drain(const NearMemoryTargetId& target);
  Result<void> cancel_plan(const ExecutionPlanId& plan_id);
  Result<void> fence_worker(const WorkerBootId& boot);
  Result<void> on_worker_disconnected(const WorkerBootId& boot);
  Result<void> retire_target(const NearMemoryTargetId& target);
  void advance_epoch();

  // ---- Persistence. ----
  Result<void> save(const std::filesystem::path& path) const;
  Result<void> load(const std::filesystem::path& path);

  // ---- Authority staleness detection (pre-dispatch revalidation). ----
  std::vector<StaleReason> staleness_of(const ExecutionPlan& plan) const;

  // ---- Inspection. ----
  std::string inspect_text() const;
  bool has_target(const NearMemoryTargetId& t) const;
  bool has_operation(const OperationId& o) const;
  bool has_plan(const ExecutionPlanId& p) const;
  bool has_result(const ExecutionPlanId& p) const;
  ExecutionResult result_of(const ExecutionPlanId& p) const;
  ExecutionLifecycle lifecycle_of(const ExecutionPlanId& p) const;
  NearMemoryTargetGeneration target_generation(const NearMemoryTargetId& t) const;
  CoordinatorEpoch current_epoch() const;
  RuntimeState state() const;  // copy for tests/persistence (bounded, not hot-path)

 private:
  Result<PlanOutput> plan_locked(const OperationId& op_id);
  std::vector<StaleReason> staleness_locked(const ExecutionPlan& plan) const;
  void rebuild_active_plans_locked();

  mutable std::mutex mu_;
  RuntimeState st_;
};

}  // namespace nmc
