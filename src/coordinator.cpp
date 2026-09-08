#include "nmc/coordinator.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

#include "nmc/digest.hpp"
#include "nmc/util.hpp"

namespace nmc {

namespace {
constexpr std::uint64_t kDefaultConventionalBandwidthBps = 40'000'000'000ull;
constexpr std::uint64_t kDefaultConventionalLatencyNs = 50'000ull;
constexpr std::uint64_t kMaxSamplePayload = 16 * 1024;      // 16 KiB representative input
constexpr std::size_t kMaxPersistCount = 4096;
constexpr std::size_t kMaxPersistHistory = 256;

// Deterministic representative payload derived from the operation identity. The SYNTHETIC
// engine operates on this bounded representative buffer; logical input_bytes drives economics.
std::vector<std::uint8_t> build_sample_payload(const OperationSpec& op) {
  const std::uint64_t n = std::min<std::uint64_t>(op.input_bytes, kMaxSamplePayload);
  std::vector<std::uint8_t> out(static_cast<std::size_t>(n));
  std::uint64_t state = op.id.value() ^ 0x9E3779B97F4A7C15ull;
  for (auto& b : out) {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    b = static_cast<std::uint8_t>((state >> 33) & 0xFF);
  }
  return out;
}

template <class T>
void put_id(ByteWriter& w, const Id<T>& id) { w.write_u64(id.value()); }
template <class T>
bool get_id(ByteReader& r, Id<T>& id) {
  std::uint64_t v = 0;
  if (!r.read_u64(v)) return false;
  id = Id<T>(v);
  return true;
}
template <class T>
void put_gen(ByteWriter& w, const Generation<T>& g) { w.write_u64(g.value()); }
template <class T>
bool get_gen(ByteReader& r, Generation<T>& g) {
  std::uint64_t v = 0;
  if (!r.read_u64(v)) return false;
  g = Generation<T>(v);
  return true;
}
template <class E>
void put_enum(ByteWriter& w, E e) { w.write_string(to_string(e)); }
template <class E>
bool get_enum(ByteReader& r, E& e) {
  std::string s;
  if (!r.read_string(s)) return false;
  auto p = parse_enum<E>(s);
  if (!p) return false;
  e = *p;
  return true;
}

void put_bool(ByteWriter& w, bool b) { w.write_u8(b ? 1 : 0); }
bool get_bool(ByteReader& r, bool& b) { std::uint8_t v = 0; if (!r.read_u8(v)) return false; b = (v != 0); return true; }

}  // namespace

Coordinator::Coordinator() {
  st_.coordinator_id = CoordinatorId::make();
  st_.epoch = CoordinatorEpoch(1);
  // Built-in default policy.
  Policy dflt;
  dflt.id = PolicyId(1);
  dflt.generation = PolicyGeneration(1);
  dflt.name = "default";
  dflt.outcome = PolicyOutcome::UNCONSTRAINED;
  st_.policies[dflt.id] = dflt;
}

Coordinator::Coordinator(CoordinatorId id) : Coordinator() { st_.coordinator_id = id; }

Result<void> Coordinator::attach_coordinator(CoordinatorId id) {
  std::lock_guard<std::mutex> lk(mu_);
  st_.coordinator_id = id;
  return Result<void>();
}

CoordinatorEpoch Coordinator::current_epoch() const {
  std::lock_guard<std::mutex> lk(mu_);
  return st_.epoch;
}

void Coordinator::advance_epoch() {
  std::lock_guard<std::mutex> lk(mu_);
  st_.epoch = CoordinatorEpoch(st_.epoch.value() + 1);
  // Advancing the epoch invalidates all dynamic authority and fences in-flight plans.
  for (auto& [tid, rt] : st_.target_runtime) {
    (void)tid;
    rt.dynamic_current = false;
    rt.reachable = false;
    rt.lifecycle = TargetLifecycle::REVALIDATION_REQUIRED;
  }
  for (auto& [tid, cs] : st_.capabilities) {
    (void)tid;
    cs.dynamic_current = false;
  }
  for (auto& [rid, ev] : st_.data_evidence) {
    (void)rid;
    ev.dynamic_current = false;
  }
  for (auto& [pid, plan] : st_.plans) {
    (void)pid;
    if (plan.lifecycle != ExecutionLifecycle::COMMITTED) {
      plan.lifecycle = (plan.lifecycle == ExecutionLifecycle::DISPATCHED ||
                        plan.lifecycle == ExecutionLifecycle::RUNNING ||
                        plan.lifecycle == ExecutionLifecycle::VERIFYING)
                           ? ExecutionLifecycle::OUTCOME_UNKNOWN
                           : ExecutionLifecycle::SUPERSEDED;
    }
  }
  rebuild_active_plans_locked();
}

Result<void> Coordinator::register_memory_domain(const MemoryDomainDescriptor& md) {
  std::lock_guard<std::mutex> lk(mu_);
  if (md.id.is_null() || md.generation.is_null()) return make_error("E_ID_NULL", "null memory-domain identity");
  if (st_.memory_domains.count(md.id)) return make_error("E_DUP", "duplicate memory domain id");
  st_.memory_domains[md.id] = md;
  return Result<void>();
}

Result<void> Coordinator::register_dataset(const DatasetDescriptor& ds) {
  std::lock_guard<std::mutex> lk(mu_);
  if (ds.id.is_null() || ds.generation.is_null()) return make_error("E_ID_NULL", "null dataset identity");
  if (st_.datasets.count(ds.id)) return make_error("E_DUP", "duplicate dataset id");
  st_.datasets[ds.id] = ds;
  return Result<void>();
}

Result<void> Coordinator::register_region(const DataRegionDescriptor& rg) {
  std::lock_guard<std::mutex> lk(mu_);
  if (rg.id.is_null() || rg.generation.is_null()) return make_error("E_ID_NULL", "null region identity");
  if (st_.regions.count(rg.id)) return make_error("E_DUP", "duplicate region id");
  st_.regions[rg.id] = rg;
  return Result<void>();
}

Result<void> Coordinator::register_program(const Program& pg) {
  std::lock_guard<std::mutex> lk(mu_);
  if (pg.id.is_null() || pg.generation.is_null()) return make_error("E_ID_NULL", "null program identity");
  if (st_.programs.count(pg.id)) return make_error("E_DUP", "duplicate program id");
  st_.programs[pg.id] = pg;
  return Result<void>();
}

Result<void> Coordinator::register_policy(const Policy& pl) {
  std::lock_guard<std::mutex> lk(mu_);
  if (pl.id.is_null() || pl.generation.is_null()) return make_error("E_ID_NULL", "null policy identity");
  if (pl.id == PolicyId(1)) return make_error("E_RESERVED", "policy id 1 is the built-in default");
  if (st_.policies.count(pl.id)) return make_error("E_DUP", "duplicate policy id");
  st_.policies[pl.id] = pl;
  return Result<void>();
}

Result<void> Coordinator::create_operation(const OperationSpec& op) {
  std::lock_guard<std::mutex> lk(mu_);
  if (op.id.is_null() || op.generation.is_null()) return make_error("E_ID_NULL", "null operation identity");
  if (st_.operations.count(op.id)) return make_error("E_DUP", "duplicate operation id");
  st_.operations[op.id] = op;
  return Result<void>();
}

Result<void> Coordinator::bind_worker_session(const WorkerId& w, const WorkerBootId& boot, const std::string& name) {
  std::lock_guard<std::mutex> lk(mu_);
  WorkerSession s;
  s.worker = w;
  s.boot = boot;
  s.name = name;
  s.connected = true;
  st_.workers[w] = s;
  return Result<void>();
}

Result<void> Coordinator::apply_register_target(const RegisterTargetMsg& msg) {
  std::lock_guard<std::mutex> lk(mu_);
  if (msg.target.is_null() || msg.generation.is_null()) return make_error("E_ID_NULL", "null target identity");
  TargetDefinition def;
  def.id = msg.target;
  def.generation = msg.generation;
  def.provider = msg.provider;
  def.kind = msg.kind;
  def.name = msg.name;
  def.synthetic = msg.synthetic;
  def.supported_ops = msg.ops;
  def.supported_data_types = msg.data_types;
  def.supported_layouts = msg.layouts;
  def.max_input_bytes = msg.max_input_bytes;
  def.alignment_constraint = msg.alignment;
  def.concurrency_capacity = msg.concurrency;
  def.workspace_bytes = msg.workspace;
  def.authority_owner = "worker";

  TargetRuntime rt;
  rt.id = msg.target;
  rt.owner_boot = msg.owner_boot;
  rt.lifecycle = TargetLifecycle::REGISTERED;
  rt.reachable = false;
  rt.dynamic_current = false;  // until a readiness heartbeat arrives

  st_.targets[msg.target] = def;
  st_.target_runtime[msg.target] = rt;
  return Result<void>();
}

Result<void> Coordinator::apply_capability(const CapabilityMsg& msg) {
  std::lock_guard<std::mutex> lk(mu_);
  if (msg.target.is_null()) return make_error("E_ID_NULL", "null target id");
  auto tit = st_.targets.find(msg.target);
  if (tit == st_.targets.end()) return make_error("E_TARGET_UNKNOWN", "target not registered");
  CapabilitySet cs;
  cs.target = msg.target;
  cs.target_generation = tit->second.generation;
  cs.generation = msg.generation;
  cs.evidence_id = EvidenceId::make();
  cs.evidence_generation = msg.evidence_generation;
  cs.publisher = msg.publisher;
  for (auto& e : msg.entries) {
    CapabilityEntry en;
    en.id = CapabilityId::make();
    en.generation = CapabilityGeneration(1);
    en.target = msg.target;
    en.key = e.key;
    en.state = e.state;
    en.required_for_execution = true;
    cs.entries[e.key] = en;
  }
  cs.dynamic_current = true;
  st_.capabilities[msg.target] = cs;
  return Result<void>();
}

Result<void> Coordinator::apply_data_evidence(const DataEvidenceMsg& msg) {
  std::lock_guard<std::mutex> lk(mu_);
  if (msg.region.is_null()) return make_error("E_ID_NULL", "null region id");
  DataEvidence ev;
  ev.region = msg.region;
  ev.region_generation = msg.region_generation;
  ev.dataset_generation = msg.dataset_generation;
  ev.memory_domain = msg.memory_domain;
  ev.memory_domain_generation = msg.memory_domain_generation;
  ev.present = msg.present;
  ev.current = msg.current;
  ev.reachable = msg.reachable;
  ev.dynamic_current = true;
  ev.publisher = msg.publisher;
  st_.data_evidence[msg.region] = ev;
  return Result<void>();
}

Result<void> Coordinator::apply_heartbeat(const HeartbeatMsg& msg) {
  std::lock_guard<std::mutex> lk(mu_);
  auto rit = st_.target_runtime.find(msg.target);
  if (rit == st_.target_runtime.end()) return make_error("E_TARGET_UNKNOWN", "target runtime not found");
  TargetRuntime& rt = rit->second;
  // A terminal target cannot be resurrected by a heartbeat.
  if (rt.lifecycle == TargetLifecycle::RETIRED || rt.lifecycle == TargetLifecycle::FAILED)
    return make_error("E_TARGET_TERMINAL", "target is terminal; heartbeat ignored");
  rt.lifecycle = msg.lifecycle;
  rt.reachable = msg.reachable;
  rt.current_queue_depth = msg.queue_depth;
  rt.performance.latency_ns = msg.latency_ns;
  rt.performance.throughput_bps = msg.throughput_bps;
  rt.performance.confidence = msg.confidence;
  rt.performance.measured = false;  // dynamic observation
  rt.dynamic_current = true;
  return Result<void>();
}

void Coordinator::rebuild_active_plans_locked() {
  st_.active_plans.clear();
  for (const auto& [pid, plan] : st_.plans) {
    (void)pid;
    if (plan.lifecycle != ExecutionLifecycle::COMMITTED && plan.lifecycle != ExecutionLifecycle::FAILED &&
        plan.lifecycle != ExecutionLifecycle::CANCELLED && plan.lifecycle != ExecutionLifecycle::SUPERSEDED &&
        plan.lifecycle != ExecutionLifecycle::FENCED) {
      st_.active_plans[plan.operation] = plan.id;
    }
  }
}

std::vector<StaleReason> Coordinator::staleness_of(const ExecutionPlan& plan) const {
  std::lock_guard<std::mutex> lk(mu_);
  return staleness_locked(plan);
}

std::vector<StaleReason> Coordinator::staleness_locked(const ExecutionPlan& plan) const {
  std::vector<StaleReason> reasons;
  const auto& s = plan.authority.snapshot;
  if (st_.epoch != s.epoch) reasons.push_back({"epoch", "coordinator epoch moved"});

  const auto tdef = st_.targets.find(plan.target);
  const auto trt = st_.target_runtime.find(plan.target);
  const auto oit = st_.operations.find(plan.operation);
  const auto cit = st_.capabilities.find(plan.target);
  const auto eit = st_.data_evidence.find(plan.region);
  const auto dit = st_.datasets.find(plan.dataset);
  const auto rit = st_.regions.find(plan.region);

  if (trt != st_.target_runtime.end() && trt->second.owner_boot != s.worker_boot)
    reasons.push_back({"worker_boot", "target owned by a different worker boot"});
  if (tdef != st_.targets.end() && tdef->second.generation != s.target_generation)
    reasons.push_back({"target_generation", "target generation moved"});
  if (dit != st_.datasets.end() && dit->second.generation != s.dataset_generation)
    reasons.push_back({"dataset_generation", "dataset generation moved"});
  if (rit != st_.regions.end() && rit->second.generation != s.region_generation)
    reasons.push_back({"region_generation", "region generation moved"});
  if (oit != st_.operations.end() && oit->second.generation != s.operation_generation)
    reasons.push_back({"operation_generation", "operation generation moved"});
  if (cit != st_.capabilities.end() && cit->second.generation != s.capability_generation)
    reasons.push_back({"capability_generation", "capability generation moved"});

  if (s.program_generation.has_value() && oit != st_.operations.end() && oit->second.program.has_value()) {
    const auto pit = st_.programs.find(oit->second.program.value());
    if (pit != st_.programs.end() && pit->second.generation != s.program_generation.value())
      reasons.push_back({"program_generation", "program generation moved"});
  }
  if (oit != st_.operations.end()) {
    const auto polit = st_.policies.find(oit->second.policy);
    if (polit != st_.policies.end() && polit->second.generation != s.policy_generation)
      reasons.push_back({"policy_generation", "policy generation moved"});
  }
  if (cit != st_.capabilities.end() && cit->second.evidence_generation != s.evidence_generation)
    reasons.push_back({"evidence_generation", "evidence generation moved"});

  if (trt != st_.target_runtime.end() && !trt->second.dynamic_current)
    reasons.push_back({"target_dynamic", "target dynamic evidence requires revalidation"});
  if (cit != st_.capabilities.end() && !cit->second.dynamic_current)
    reasons.push_back({"capability_dynamic", "capability evidence requires revalidation"});
  if (eit != st_.data_evidence.end()) {
    if (!eit->second.dynamic_current) reasons.push_back({"data_dynamic", "data evidence requires revalidation"});
    if (eit->second.dataset_generation != s.dataset_generation)
      reasons.push_back({"dataset_generation", "data current/observed generation mismatch"});
  }
  return reasons;
}

Result<Coordinator::PlanOutput> Coordinator::plan(const OperationId& op_id) {
  std::lock_guard<std::mutex> lk(mu_);
  return plan_locked(op_id);
}

Result<Coordinator::PlanOutput> Coordinator::plan_locked(const OperationId& op_id) {
  PlanOutput out;
  const auto oit = st_.operations.find(op_id);
  if (oit == st_.operations.end()) return make_error("E_PLAN_OP", "operation not registered");
  const OperationSpec& op = oit->second;

  const auto polit = st_.policies.find(op.policy);
  if (polit == st_.policies.end()) return make_error("E_PLAN_POLICY", "operation references a missing policy");
  const Policy& policy = polit->second;

  const auto data_it = st_.data_evidence.find(op.region);
  const DataEvidence* data = (data_it != st_.data_evidence.end()) ? &data_it->second : nullptr;

  // Duplicate active-plan suppression for the same operation (O(1) via the hot-path index).
  if (st_.active_plans.count(op.id))
    return make_error("E_PLAN_ACTIVE", "an active plan already exists for this operation");

  // Candidate filtering (hard eligibility first, then economics, then ranking).
  for (const auto& [tid, tdef] : st_.targets) {
    const auto trt = st_.target_runtime.find(tid);
    if (trt == st_.target_runtime.end()) continue;
    const auto cit = st_.capabilities.find(tid);
    if (cit == st_.capabilities.end()) continue;

    const Program* prog = nullptr;
    if (op.program.has_value()) {
      const auto pit = st_.programs.find(op.program.value());
      if (pit != st_.programs.end()) prog = &pit->second;
    }

    HardFilterInput in;
    in.target = &tdef;
    in.runtime = &trt->second;
    in.op = &op;
    in.data = data;
    in.caps = &cit->second;
    in.program = prog;
    in.policy = &policy;
    in.current_worker_boot = &trt->second.owner_boot;
    auto& cur_target_gen = st_.targets[tid].generation;
    in.current_target_generation = &cur_target_gen;

    auto reasons = hard_filter(in);
    if (!reasons.empty()) {
      for (auto& r : reasons)
        out.rejected.push_back(r);
      out.explanation.push_back("target " + std::to_string(tid.value()) + " rejected: " +
                                to_string(reasons.front().reason));
      continue;
    }

    // Economics.
    const PerfEstimate& perf = trt->second.performance;
    TransferEstimate tr;
    tr.bytes_to_move = op.input_bytes;
    tr.bandwidth_bps = kDefaultConventionalBandwidthBps;
    tr.latency_ns = kDefaultConventionalLatencyNs;
    tr.staging_cost_ns = 0;
    tr.serialization_cost_ns = 0;
    tr.registration_cost_ns = 0;

    ComputeCostEstimate conv;
    conv.execution_ns = op.input_bytes;  // documented deterministic default model
    conv.setup_ns = 0;
    conv.queue_delay_ns = 0;
    conv.result_return_bytes = op.output_bytes;

    ComputeCostEstimate near;
    near.execution_ns = perf.latency_ns;
    near.setup_ns = perf.setup_ns;
    near.queue_delay_ns = perf.latency_ns;  // conservatively modeled queue delay
    near.result_return_bytes = op.output_bytes;
    near.confidence = perf.confidence;

    EconomicsResult econ = choose_path(tr, conv, near);
    RankedCandidate cand;
    cand.target = tid;
    cand.generation = tdef.generation;
    cand.decision = econ.decision;
    cand.movement_avoided_bytes = econ.movement_avoided_bytes;
    cand.total_cost_ns = econ.near_total_ns;
    cand.estimated_latency_ns = near.execution_ns + near.setup_ns + near.queue_delay_ns;
    cand.confidence = perf.confidence;
    for (auto& line : econ.explanation) cand.factors.push_back(line);
    out.ranked.push_back(std::move(cand));
  }

  out.ranked = rank_candidates(std::move(out.ranked));

  if (out.ranked.empty()) {
    out.decision = (policy.outcome == PolicyOutcome::NEAR_MEMORY_REQUIRED)
                       ? Decision::REJECT
                       : Decision::MOVEMENT_REQUIRED;
    out.explanation.push_back("no eligible near-memory target; " + std::string(to_string(out.decision)));
    return out;
  }

  const RankedCandidate& best = out.ranked.front();
  bool beneficial = best.decision == Decision::NEAR_MEMORY_SELECTED;
  if (!beneficial) {
    out.decision = best.decision;
    out.explanation.push_back("best eligible candidate still not economically justified; " +
                              std::string(to_string(out.decision)));
    return out;
  }

  // Build the execution plan bound to current authority.
  const auto& trt = st_.target_runtime[best.target];
  const auto& tdef = st_.targets[best.target];
  const auto& cit = st_.capabilities[best.target];

  ExecutionPlan plan;
  plan.id = ExecutionPlanId::make();
  plan.generation = ExecutionPlanGeneration(1);
  plan.target = best.target;
  plan.target_generation = tdef.generation;
  plan.operation = op.id;
  plan.op_class = op.op_class;
  plan.dataset = op.dataset;
  plan.dataset_generation = op.dataset_generation;
  plan.region = op.region;
  plan.region_generation = op.region_generation;
  plan.lifecycle = ExecutionLifecycle::PLANNED;
  plan.retry_class = op.retry_class;
  plan.decision = Decision::NEAR_MEMORY_SELECTED;

  AuthoritySnapshot snap;
  snap.epoch = st_.epoch;
  snap.worker_boot = trt.owner_boot;
  snap.target = best.target;
  snap.target_generation = tdef.generation;
  snap.memory_domain_generation = data ? data->memory_domain_generation : MemoryDomainGeneration(1);
  snap.dataset_generation = op.dataset_generation;
  snap.region_generation = op.region_generation;
  snap.operation_generation = op.generation;
  snap.program_generation = op.program_generation;
  snap.capability_generation = cit.generation;
  snap.policy_generation = op.policy_generation;
  snap.evidence_generation = cit.evidence_generation;
  snap.plan_generation = plan.generation;
  plan.authority.snapshot = snap;
  plan.authority.plan = plan.id;
  plan.authority.plan_generation = plan.generation;

  st_.plans[plan.id] = plan;
  st_.active_plans[op.id] = plan.id;
  out.plan = plan;
  out.decision = Decision::NEAR_MEMORY_SELECTED;
  out.created = true;
  out.explanation.push_back("selected target " + std::to_string(best.target.value()) + " (" + tdef.name + ")");
  return out;
}

Result<void> Coordinator::reserve(const ExecutionPlanId& plan_id) {
  std::lock_guard<std::mutex> lk(mu_);
  auto pit = st_.plans.find(plan_id);
  if (pit == st_.plans.end()) return make_error("E_PLAN_NOT_FOUND", "plan not found");
  ExecutionPlan& plan = pit->second;
  if (plan.lifecycle != ExecutionLifecycle::PLANNED)
    return make_error("E_PLAN_STATE", "plan not in PLANNED state for reservation");
  auto trt = st_.target_runtime.find(plan.target);
  if (trt == st_.target_runtime.end()) return make_error("E_TARGET_UNKNOWN", "target runtime missing");
  const TargetDefinition& tdef = st_.targets[plan.target];

  // Admission check (atomic under the lock).
  ReservationState& rs = st_.reservations[plan.target];
  if (rs.reserved_slots >= tdef.concurrency_capacity)
    return make_error("E_CONCURRENCY", "target concurrency capacity exhausted");
  if (rs.reserved_workspace > tdef.workspace_bytes)
    return make_error("E_WORKSPACE", "target workspace exhausted");

  // Drain gating.
  if (trt->second.lifecycle == TargetLifecycle::DRAINING || trt->second.lifecycle == TargetLifecycle::DEGRADED ||
      trt->second.lifecycle == TargetLifecycle::REVALIDATION_REQUIRED)
    return make_error("E_DRAIN", "target not accepting new admission");

  std::uint64_t op_ws = 0;
  auto oit = st_.operations.find(plan.operation);
  if (oit != st_.operations.end()) op_ws = oit->second.workspace_bytes;
  if (rs.reserved_workspace + op_ws > tdef.workspace_bytes)
    return make_error("E_WORKSPACE", "target workspace budget exceeded");

  ++rs.reserved_slots;
  rs.reserved_workspace += op_ws;
  plan.reserved_slots = 1;
  plan.reserved_workspace = op_ws;
  plan.lifecycle = ExecutionLifecycle::RESERVED;
  return Result<void>();
}

Result<Coordinator::DispatchOutput> Coordinator::prepare_dispatch(const ExecutionPlanId& plan_id,
                                                                  const DispatchId& dispatch_id,
                                                                  const AttemptId& attempt_id) {
  std::lock_guard<std::mutex> lk(mu_);
  auto pit = st_.plans.find(plan_id);
  if (pit == st_.plans.end()) return make_error("E_PLAN_NOT_FOUND", "plan not found");
  ExecutionPlan& plan = pit->second;
  if (plan.lifecycle != ExecutionLifecycle::RESERVED)
    return make_error("E_PLAN_STATE", "plan must be RESERVED before dispatch");

  auto stale = staleness_locked(plan);
  if (!stale.empty()) {
    plan.lifecycle = ExecutionLifecycle::SUPERSEDED;
    std::string why;
    for (auto& s : stale) why += s.dimension + "; ";
    return make_error("E_STALE_PLAN", "plan stale before dispatch: " + why);
  }

  const auto oit = st_.operations.find(plan.operation);
  if (oit == st_.operations.end()) return make_error("E_PLAN_OP", "operation missing at dispatch");
  const OperationSpec& op = oit->second;

  const auto trt = st_.target_runtime.find(plan.target);
  if (trt == st_.target_runtime.end()) return make_error("E_TARGET_UNKNOWN", "target runtime missing");
  const WorkerBootId boot = trt->second.owner_boot;

  // Find the worker session for this boot.
  WorkerId worker;
  bool found = false;
  for (const auto& [wid, ws] : st_.workers) {
    if (ws.boot == boot) { worker = wid; found = true; break; }
  }
  if (!found) return make_error("E_WORKER_UNKNOWN", "no worker session bound to target boot");

  DispatchOutput out;
  out.ready = true;
  out.worker = worker;
  DispatchMsg& m = out.message;
  m.plan = plan.id;
  m.plan_gen = plan.generation;
  m.dispatch = dispatch_id;
  m.attempt = attempt_id;
  m.target = plan.target;
  m.target_gen = plan.target_generation;
  m.worker_boot = boot;
  m.epoch = st_.epoch;
  m.dataset_gen = plan.dataset_generation;
  m.region_gen = plan.region_generation;
  m.operation_gen = op.generation;
  m.has_program = op.program.has_value();
  if (op.program.has_value()) m.program_gen = op.program_generation.value_or(ProgramGeneration(1));
  m.op_class = op.op_class;
  m.data_type = op.shape.data_type;
  m.retry_class = op.retry_class;
  m.idempotent = (op.retry_class == RetryClass::IDEMPOTENT || op.retry_class == RetryClass::REPLAY_SAFE);
  m.require_verification = (op.integrity == IntegrityRequirement::CHECKED || op.integrity == IntegrityRequirement::STRICT);
  m.payload = build_sample_payload(op);

  plan.dispatch_id = dispatch_id;
  plan.attempt_id = attempt_id;
  return out;
}

Result<void> Coordinator::confirm_dispatched(const ExecutionPlanId& plan_id, const DispatchId& dispatch_id) {
  std::lock_guard<std::mutex> lk(mu_);
  auto pit = st_.plans.find(plan_id);
  if (pit == st_.plans.end()) return make_error("E_PLAN_NOT_FOUND", "plan not found");
  ExecutionPlan& plan = pit->second;
  if (!plan.dispatch_id.has_value() || plan.dispatch_id.value() != dispatch_id)
    return make_error("E_DISPATCH_MISMATCH", "dispatch id mismatch");
  if (plan.lifecycle != ExecutionLifecycle::RESERVED)
    return make_error("E_PLAN_STATE", "plan not in RESERVED state");
  plan.lifecycle = ExecutionLifecycle::DISPATCHED;
  return Result<void>();
}

Result<ExecutionResult> Coordinator::apply_result(const ResultMsg& msg) {
  std::lock_guard<std::mutex> lk(mu_);
  auto pit = st_.plans.find(msg.plan);
  if (pit == st_.plans.end()) return make_error("E_RESULT_PLAN", "result references unknown plan");
  ExecutionPlan& plan = pit->second;

  if (plan.lifecycle == ExecutionLifecycle::COMMITTED)
    return make_error("E_RESULT_DUP", "plan already committed (duplicate completion)");
  if (plan.lifecycle == ExecutionLifecycle::CANCELLED || plan.lifecycle == ExecutionLifecycle::FENCED ||
      plan.lifecycle == ExecutionLifecycle::SUPERSEDED)
    return make_error("E_RESULT_STALE", "completion after cancellation/supersession");
  if (plan.lifecycle != ExecutionLifecycle::DISPATCHED && plan.lifecycle != ExecutionLifecycle::RUNNING &&
      plan.lifecycle != ExecutionLifecycle::VERIFYING)
    return make_error("E_RESULT_STATE", "completion in unexpected plan state");

  // Current-authority validation against the live coordinator state BEFORE mutation.
  if (msg.epoch != st_.epoch)
    return make_error("E_RESULT_STALE", "completion carries a stale coordinator epoch");
  const auto tit = st_.targets.find(msg.target);
  if (tit != st_.targets.end() && tit->second.generation != msg.target_gen)
    return make_error("E_RESULT_STALE", "completion carries a stale target generation");
  const auto trt = st_.target_runtime.find(msg.target);
  if (trt != st_.target_runtime.end() && trt->second.owner_boot != msg.worker_boot)
    return make_error("E_RESULT_STALE", "completion carries a stale worker boot");

  // Authority validation BEFORE mutation.
  CompletionAuthority ca;
  ca.plan = msg.plan;
  ca.plan_generation = msg.plan_gen;
  ca.dispatch = msg.dispatch;
  ca.attempt = msg.attempt;
  ca.target = msg.target;
  ca.target_generation = msg.target_gen;
  ca.worker_boot = msg.worker_boot;
  ca.epoch = msg.epoch;
  ca.dataset_generation = msg.dataset_gen;
  ca.region_generation = msg.region_gen;
  ca.operation_generation = msg.operation_gen;
  ca.result_generation = msg.result_gen;
  auto stale = validate_completion_against_plan(ca, plan.authority);
  if (!stale.empty()) {
    std::string why;
    for (auto& s : stale) why += s.dimension + "; ";
    return make_error("E_RESULT_STALE", "stale completion authority: " + why);
  }
  if (!plan.dispatch_id.has_value() || plan.dispatch_id.value() != msg.dispatch)
    return make_error("E_DISPATCH_MISMATCH", "dispatch id mismatch");
  if (!plan.attempt_id.has_value() || plan.attempt_id.value() != msg.attempt)
    return make_error("E_ATTEMPT_MISMATCH", "attempt id mismatch");

  ExecutionResult res;
  res.id = ResultId::make();
  res.generation = msg.result_gen;
  res.plan = msg.plan;
  res.plan_generation = msg.plan_gen;
  res.dispatch = msg.dispatch;
  res.attempt = msg.attempt;
  res.target = msg.target;
  res.target_generation = msg.target_gen;
  res.worker_boot = msg.worker_boot;
  res.epoch = msg.epoch;
  res.dataset_generation = msg.dataset_gen;
  res.region_generation = msg.region_gen;
  res.operation_generation = msg.operation_gen;
  res.output_bytes = msg.output.size();
  res.note = msg.note;

  // OUTCOME_UNKNOWN: the target may have executed but completion was not confirmable. We must
  // not invent success or failure.
  if (msg.unknown) {
    res.ambiguous = true;
    res.verification = VerifyState::UNKNOWN;
    plan.lifecycle = ExecutionLifecycle::OUTCOME_UNKNOWN;
    st_.active_plans.erase(plan.operation);
    st_.results[res.id] = res;
    return res;
  }

  // Integrity verification. A completion must not become authoritative if verification fails.
  auto digest = content_digest(std::span<const std::uint8_t>(msg.output.data(), msg.output.size()));
  bool intact = (to_hex(digest) == msg.output_digest);
  if (!intact) {
    res.verification = VerifyState::MISMATCH;
    res.output_digest = msg.output_digest;
    plan.lifecycle = ExecutionLifecycle::FAILED;
    st_.active_plans.erase(plan.operation);
    st_.results[res.id] = res;
    return res;
  }

  res.verification = VerifyState::VERIFIED;
  res.output_digest = msg.output_digest;
  res.committed = true;
  plan.lifecycle = ExecutionLifecycle::COMMITTED;
  st_.active_plans.erase(plan.operation);
  st_.results[res.id] = res;

  // Release reservation.
  auto rs = st_.reservations.find(plan.target);
  if (rs != st_.reservations.end()) {
    if (rs->second.reserved_slots > 0) --rs->second.reserved_slots;
    if (rs->second.reserved_workspace >= plan.reserved_workspace) rs->second.reserved_workspace -= plan.reserved_workspace;
  }
  return res;
}

Result<void> Coordinator::begin_drain(const NearMemoryTargetId& target) {
  std::lock_guard<std::mutex> lk(mu_);
  auto trt = st_.target_runtime.find(target);
  if (trt == st_.target_runtime.end()) return make_error("E_TARGET_UNKNOWN", "target runtime not found");
  if (trt->second.lifecycle == TargetLifecycle::RETIRED || trt->second.lifecycle == TargetLifecycle::FAILED)
    return make_error("E_TARGET_STATE", "target is terminal");
  trt->second.lifecycle = TargetLifecycle::DRAINING;
  return Result<void>();
}

Result<void> Coordinator::cancel_plan(const ExecutionPlanId& plan_id) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = st_.plans.find(plan_id);
  if (it == st_.plans.end()) return make_error("E_PLAN_NOT_FOUND", "plan not found");
  ExecutionPlan& plan = it->second;
  if (plan.lifecycle == ExecutionLifecycle::COMMITTED || plan.lifecycle == ExecutionLifecycle::FAILED ||
      plan.lifecycle == ExecutionLifecycle::CANCELLED || plan.lifecycle == ExecutionLifecycle::SUPERSEDED ||
      plan.lifecycle == ExecutionLifecycle::FENCED)
    return make_error("E_PLAN_STATE", "plan is terminal; cannot cancel");
  auto rs = st_.reservations.find(plan.target);
  if (rs != st_.reservations.end()) {
    if (rs->second.reserved_slots > 0) --rs->second.reserved_slots;
    if (rs->second.reserved_workspace >= plan.reserved_workspace) rs->second.reserved_workspace -= plan.reserved_workspace;
  }
  plan.lifecycle = ExecutionLifecycle::CANCELLED;
  st_.active_plans.erase(plan.operation);
  return Result<void>();
}

Result<void> Coordinator::fence_worker(const WorkerBootId& boot) {
  std::lock_guard<std::mutex> lk(mu_);
  for (auto& [tid, rt] : st_.target_runtime) {
    (void)tid;
    if (rt.owner_boot == boot) {
      rt.dynamic_current = false;
      rt.reachable = false;
      rt.lifecycle = TargetLifecycle::REVALIDATION_REQUIRED;
    }
  }
  for (auto& [tid, cs] : st_.capabilities) {
    (void)tid;
    if (cs.publisher == boot) cs.dynamic_current = false;
  }
  for (auto& [rid, ev] : st_.data_evidence) {
    (void)rid;
    if (ev.publisher == boot) ev.dynamic_current = false;
  }
  for (auto& [pid, plan] : st_.plans) {
    (void)pid;
    if (plan.authority.snapshot.worker_boot == boot && plan.lifecycle != ExecutionLifecycle::COMMITTED &&
        plan.lifecycle != ExecutionLifecycle::FAILED && plan.lifecycle != ExecutionLifecycle::CANCELLED &&
        plan.lifecycle != ExecutionLifecycle::SUPERSEDED && plan.lifecycle != ExecutionLifecycle::FENCED) {
      plan.lifecycle = (plan.lifecycle == ExecutionLifecycle::DISPATCHED ||
                        plan.lifecycle == ExecutionLifecycle::RUNNING ||
                        plan.lifecycle == ExecutionLifecycle::VERIFYING)
                           ? ExecutionLifecycle::OUTCOME_UNKNOWN
                           : ExecutionLifecycle::FENCED;
    }
  }
  rebuild_active_plans_locked();
  return Result<void>();
}

Result<void> Coordinator::on_worker_disconnected(const WorkerBootId& boot) {
  std::lock_guard<std::mutex> lk(mu_);
  for (auto& [wid, ws] : st_.workers) {
    (void)wid;
    if (ws.boot == boot) ws.connected = false;
  }
  // Apply the same invalidation as fence_worker without re-locking.
  for (auto& [tid, rt] : st_.target_runtime) {
    (void)tid;
    if (rt.owner_boot == boot) { rt.dynamic_current = false; rt.reachable = false; rt.lifecycle = TargetLifecycle::REVALIDATION_REQUIRED; }
  }
  for (auto& [tid, cs] : st_.capabilities) {
    (void)tid;
    if (cs.publisher == boot) cs.dynamic_current = false;
  }
  for (auto& [rid, ev] : st_.data_evidence) {
    (void)rid;
    if (ev.publisher == boot) ev.dynamic_current = false;
  }
  for (auto& [pid, plan] : st_.plans) {
    (void)pid;
    if (plan.authority.snapshot.worker_boot == boot && plan.lifecycle != ExecutionLifecycle::COMMITTED &&
        plan.lifecycle != ExecutionLifecycle::FAILED && plan.lifecycle != ExecutionLifecycle::CANCELLED &&
        plan.lifecycle != ExecutionLifecycle::SUPERSEDED && plan.lifecycle != ExecutionLifecycle::FENCED) {
      plan.lifecycle = (plan.lifecycle == ExecutionLifecycle::DISPATCHED ||
                        plan.lifecycle == ExecutionLifecycle::RUNNING ||
                        plan.lifecycle == ExecutionLifecycle::VERIFYING)
                           ? ExecutionLifecycle::OUTCOME_UNKNOWN
                           : ExecutionLifecycle::FENCED;
    }
  }
  rebuild_active_plans_locked();
  return Result<void>();
}

Result<void> Coordinator::retire_target(const NearMemoryTargetId& target) {
  std::lock_guard<std::mutex> lk(mu_);
  auto trt = st_.target_runtime.find(target);
  if (trt == st_.target_runtime.end()) return make_error("E_TARGET_UNKNOWN", "target runtime not found");
  trt->second.lifecycle = TargetLifecycle::RETIRED;
  trt->second.reachable = false;
  trt->second.dynamic_current = false;
  for (auto& [pid, plan] : st_.plans) {
    (void)pid;
    if (plan.target == target && plan.lifecycle != ExecutionLifecycle::COMMITTED &&
        plan.lifecycle != ExecutionLifecycle::FAILED && plan.lifecycle != ExecutionLifecycle::CANCELLED &&
        plan.lifecycle != ExecutionLifecycle::SUPERSEDED && plan.lifecycle != ExecutionLifecycle::FENCED)
      plan.lifecycle = ExecutionLifecycle::FENCED;
  }
  rebuild_active_plans_locked();
  return Result<void>();
}

bool Coordinator::has_target(const NearMemoryTargetId& t) const {
  std::lock_guard<std::mutex> lk(mu_);
  return st_.targets.count(t) != 0;
}
bool Coordinator::has_operation(const OperationId& o) const {
  std::lock_guard<std::mutex> lk(mu_);
  return st_.operations.count(o) != 0;
}
bool Coordinator::has_plan(const ExecutionPlanId& p) const {
  std::lock_guard<std::mutex> lk(mu_);
  return st_.plans.count(p) != 0;
}
bool Coordinator::has_result(const ExecutionPlanId& p) const {
  std::lock_guard<std::mutex> lk(mu_);
  for (const auto& [rid, r] : st_.results) {
    (void)rid;
    if (r.plan == p) return true;
  }
  return false;
}
ExecutionResult Coordinator::result_of(const ExecutionPlanId& p) const {
  std::lock_guard<std::mutex> lk(mu_);
  for (const auto& [rid, r] : st_.results) {
    (void)rid;
    if (r.plan == p) return r;
  }
  return ExecutionResult{};
}
ExecutionLifecycle Coordinator::lifecycle_of(const ExecutionPlanId& p) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = st_.plans.find(p);
  return it == st_.plans.end() ? ExecutionLifecycle::PLANNED : it->second.lifecycle;
}
NearMemoryTargetGeneration Coordinator::target_generation(const NearMemoryTargetId& t) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = st_.targets.find(t);
  return it == st_.targets.end() ? NearMemoryTargetGeneration() : it->second.generation;
}
RuntimeState Coordinator::state() const {
  std::lock_guard<std::mutex> lk(mu_);
  return st_;
}

// ---------------------------------------------------------------------------
// Durable persistence encoding. Dynamic evidence (target_runtime, capabilities,
// data_evidence, worker sessions) is deliberately NOT persisted.
// ---------------------------------------------------------------------------
namespace {

void put_shape(ByteWriter& w, const DataShape& s) {
  put_enum(w, s.data_type);
  put_enum(w, s.layout);
  w.write_u64(s.element_count);
  w.write_u64(s.alignment);
}
bool get_shape(ByteReader& r, DataShape& s) {
  if (!get_enum(r, s.data_type) || !get_enum(r, s.layout)) return false;
  if (!r.read_u64(s.element_count) || !r.read_u64(s.alignment)) return false;
  return true;
}

void put_snapshot(ByteWriter& w, const AuthoritySnapshot& s) {
  put_gen(w, s.epoch);
  put_id(w, s.worker_boot);
  put_id(w, s.target);
  put_gen(w, s.target_generation);
  put_gen(w, s.memory_domain_generation);
  put_gen(w, s.dataset_generation);
  put_gen(w, s.region_generation);
  put_gen(w, s.operation_generation);
  put_bool(w, s.program_generation.has_value());
  if (s.program_generation.has_value()) put_gen(w, s.program_generation.value());
  put_gen(w, s.capability_generation);
  put_gen(w, s.policy_generation);
  put_gen(w, s.evidence_generation);
  put_gen(w, s.plan_generation);
}
bool get_snapshot(ByteReader& r, AuthoritySnapshot& s) {
  if (!get_gen(r, s.epoch) || !get_id(r, s.worker_boot) || !get_id(r, s.target)) return false;
  if (!get_gen(r, s.target_generation) || !get_gen(r, s.memory_domain_generation)) return false;
  if (!get_gen(r, s.dataset_generation) || !get_gen(r, s.region_generation)) return false;
  if (!get_gen(r, s.operation_generation)) return false;
  bool has_prog = false;
  if (!get_bool(r, has_prog)) return false;
  if (has_prog) {
    ProgramGeneration pg;
    if (!get_gen(r, pg)) return false;
    s.program_generation = pg;
  }
  if (!get_gen(r, s.capability_generation) || !get_gen(r, s.policy_generation)) return false;
  if (!get_gen(r, s.evidence_generation) || !get_gen(r, s.plan_generation)) return false;
  return true;
}

std::size_t bounded(std::size_t n) { return n > kMaxPersistCount ? kMaxPersistCount : n; }

void encode_state(const RuntimeState& st, ByteWriter& w) {
  put_id(w, st.coordinator_id);
  put_gen(w, st.epoch);

  std::size_t md = bounded(st.memory_domains.size());
  w.write_u64(md);
  for (const auto& [id, v] : st.memory_domains) {
    (void)id;
    put_id(w, v.id); put_gen(w, v.generation); w.write_string(v.name); put_bool(w, v.durable);
  }
  std::size_t ds = bounded(st.datasets.size());
  w.write_u64(ds);
  for (const auto& [id, v] : st.datasets) {
    (void)id;
    put_id(w, v.id); put_gen(w, v.generation); w.write_string(v.name); w.write_string(v.producer); put_bool(w, v.durable);
  }
  std::size_t rg = bounded(st.regions.size());
  w.write_u64(rg);
  for (const auto& [id, v] : st.regions) {
    (void)id;
    put_id(w, v.id); put_gen(w, v.generation); put_id(w, v.dataset); put_gen(w, v.dataset_generation);
    put_id(w, v.memory_domain); w.write_u64(v.window.offset); w.write_u64(v.window.length);
    put_shape(w, v.shape); put_enum(w, v.integrity); put_bool(w, v.durable);
  }
  std::size_t pr = bounded(st.programs.size());
  w.write_u64(pr);
  for (const auto& [id, v] : st.programs) {
    (void)id;
    put_id(w, v.id); put_gen(w, v.generation); w.write_string(v.name);
    put_enum(w, v.op_class); put_enum(w, v.target_kind); w.write_string(v.digest);
    w.write_u64(v.data_types.size());
    for (auto dt : v.data_types) put_enum(w, dt);
    w.write_u64(v.layouts.size());
    for (auto l : v.layouts) put_enum(w, l);
    w.write_u64(v.workspace_bytes); put_enum(w, v.lifecycle); put_bool(w, v.predefined); put_bool(w, v.validated);
  }
  std::size_t po = bounded(st.policies.size());
  w.write_u64(po);
  for (const auto& [id, v] : st.policies) {
    (void)id;
    put_id(w, v.id); put_gen(w, v.generation); w.write_string(v.name); put_enum(w, v.outcome);
    w.write_u64(v.permitted_fallbacks.size());
    for (auto d : v.permitted_fallbacks) put_enum(w, d);
    w.write_f64(v.required_confidence); w.write_f64(v.near_memory_bias);
    put_bool(w, v.reject_on_stale); w.write_u64(v.max_near_memory_input_bytes); put_bool(w, v.require_verification);
  }
  std::size_t op = bounded(st.operations.size());
  w.write_u64(op);
  for (const auto& [id, v] : st.operations) {
    (void)id;
    put_id(w, v.id); put_gen(w, v.generation); put_enum(w, v.op_class);
    put_id(w, v.dataset); put_gen(w, v.dataset_generation);
    put_id(w, v.region); put_gen(w, v.region_generation);
    put_shape(w, v.shape); w.write_u64(v.input_bytes); w.write_u64(v.output_bytes); w.write_u64(v.workspace_bytes);
    put_enum(w, v.output_data_type); put_bool(w, v.determinism_required);
    w.write_u64(v.max_tolerated_latency_ns); w.write_u64(v.required_throughput_bps);
    put_enum(w, v.integrity); put_enum(w, v.retry_class);
    put_id(w, v.policy); put_gen(w, v.policy_generation);
    put_bool(w, v.program.has_value());
    if (v.program.has_value()) { put_id(w, v.program.value()); put_gen(w, v.program_generation.value()); }
    w.write_string(v.description);
  }
  std::size_t tg = bounded(st.targets.size());
  w.write_u64(tg);
  for (const auto& [id, v] : st.targets) {
    (void)id;
    put_id(w, v.id); put_gen(w, v.generation); put_enum(w, v.provider); put_enum(w, v.kind);
    w.write_string(v.name); w.write_string(v.description); put_id(w, v.failure_domain);
    w.write_u64(v.memory_domains.size());
    for (auto m : v.memory_domains) put_id(w, m);
    w.write_u64(v.supported_ops.size());
    for (auto o : v.supported_ops) put_enum(w, o);
    w.write_u64(v.supported_data_types.size());
    for (auto d : v.supported_data_types) put_enum(w, d);
    w.write_u64(v.supported_layouts.size());
    for (auto l : v.supported_layouts) put_enum(w, l);
    w.write_u64(v.max_input_bytes); w.write_u64(v.alignment_constraint); w.write_u32(v.concurrency_capacity);
    w.write_u64(v.queue_depth); w.write_u64(v.workspace_bytes); put_enum(w, v.access_semantics);
    put_bool(w, v.requires_staging); put_bool(w, v.synthetic); put_bool(w, v.supports_verification);
    put_bool(w, v.supports_cancellation); put_bool(w, v.supports_drain); w.write_string(v.authority_owner);
  }
  // Bounded plan/result history.
  std::size_t pl = bounded(st.plans.size());
  w.write_u64(pl);
  for (const auto& [id, v] : st.plans) {
    (void)id;
    put_id(w, v.id); put_gen(w, v.generation); put_snapshot(w, v.authority.snapshot);
    put_id(w, v.target); put_gen(w, v.target_generation); put_id(w, v.operation);
    put_enum(w, v.op_class); put_id(w, v.dataset); put_gen(w, v.dataset_generation);
    put_id(w, v.region); put_gen(w, v.region_generation); put_enum(w, v.lifecycle);
    w.write_u64(v.reserved_slots); w.write_u64(v.reserved_workspace);
    put_bool(w, v.dispatch_id.has_value());
    if (v.dispatch_id.has_value()) put_id(w, v.dispatch_id.value());
    put_bool(w, v.attempt_id.has_value());
    if (v.attempt_id.has_value()) put_id(w, v.attempt_id.value());
    put_enum(w, v.retry_class); put_enum(w, v.decision);
    w.write_u64(v.notes.size());
    for (auto& n : v.notes) w.write_string(n);
  }
  std::size_t rs = bounded(st.results.size());
  w.write_u64(rs);
  for (const auto& [id, v] : st.results) {
    (void)id;
    put_id(w, v.id); put_gen(w, v.generation); put_id(w, v.plan); put_gen(w, v.plan_generation);
    put_id(w, v.dispatch); put_id(w, v.attempt); put_id(w, v.target); put_gen(w, v.target_generation);
    put_id(w, v.worker_boot); put_gen(w, v.epoch); put_gen(w, v.dataset_generation);
    put_gen(w, v.region_generation); put_gen(w, v.operation_generation);
    put_enum(w, v.verification); w.write_string(v.output_digest); w.write_u64(v.output_bytes);
    put_bool(w, v.committed); put_bool(w, v.ambiguous); w.write_string(v.note);
  }
}

bool decode_state(RuntimeState& st, ByteReader& r) {
  if (!get_id(r, st.coordinator_id) || !get_gen(r, st.epoch)) return false;
  if (st.epoch.is_null()) return false;
  std::uint64_t n = 0;
  if (!r.read_u64(n) || n > kMaxPersistCount) return false;
  for (std::uint64_t i = 0; i < n; ++i) { MemoryDomainDescriptor v; if (!get_id(r, v.id) || !get_gen(r, v.generation) || !r.read_string(v.name) || !get_bool(r, v.durable)) return false; st.memory_domains[v.id] = v; }
  if (!r.read_u64(n) || n > kMaxPersistCount) return false;
  for (std::uint64_t i = 0; i < n; ++i) { DatasetDescriptor v; if (!get_id(r, v.id) || !get_gen(r, v.generation) || !r.read_string(v.name) || !r.read_string(v.producer) || !get_bool(r, v.durable)) return false; st.datasets[v.id] = v; }
  if (!r.read_u64(n) || n > kMaxPersistCount) return false;
  for (std::uint64_t i = 0; i < n; ++i) { DataRegionDescriptor v; std::uint64_t o=0,l=0; if (!get_id(r, v.id) || !get_gen(r, v.generation) || !get_id(r, v.dataset) || !get_gen(r, v.dataset_generation) || !get_id(r, v.memory_domain) || !r.read_u64(o) || !r.read_u64(l) || !get_shape(r, v.shape) || !get_enum(r, v.integrity) || !get_bool(r, v.durable)) return false; v.window.offset = o; v.window.length = l; st.regions[v.id] = v; }
  if (!r.read_u64(n) || n > kMaxPersistCount) return false;
  for (std::uint64_t i = 0; i < n; ++i) { Program v; std::uint64_t ndt=0,nl=0; if (!get_id(r, v.id) || !get_gen(r, v.generation) || !r.read_string(v.name) || !get_enum(r, v.op_class) || !get_enum(r, v.target_kind) || !r.read_string(v.digest) || !r.read_u64(ndt)) return false; for (std::uint64_t j=0;j<ndt && j<64;++j){ DataType dt; if(!get_enum(r,dt)) return false; v.data_types.push_back(dt);} if(!r.read_u64(nl)) return false; for(std::uint64_t j=0;j<nl && j<64;++j){ Layout l; if(!get_enum(r,l)) return false; v.layouts.push_back(l);} if(!r.read_u64(v.workspace_bytes)||!get_enum(r,v.lifecycle)||!get_bool(r,v.predefined)||!get_bool(r,v.validated)) return false; st.programs[v.id] = v; }
  if (!r.read_u64(n) || n > kMaxPersistCount) return false;
  for (std::uint64_t i = 0; i < n; ++i) { Policy v; std::uint64_t nf=0; if (!get_id(r, v.id) || !get_gen(r, v.generation) || !r.read_string(v.name) || !get_enum(r, v.outcome) || !r.read_u64(nf)) return false; for(std::uint64_t j=0;j<nf && j<64;++j){ Decision d; if(!get_enum(r,d)) return false; v.permitted_fallbacks.push_back(d);} if(!r.read_f64(v.required_confidence)||!r.read_f64(v.near_memory_bias)||!get_bool(r,v.reject_on_stale)||!r.read_u64(v.max_near_memory_input_bytes)||!get_bool(r,v.require_verification)) return false; st.policies[v.id] = v; }
  if (!r.read_u64(n) || n > kMaxPersistCount) return false;
  for (std::uint64_t i = 0; i < n; ++i) { OperationSpec v; bool has_prog=false; if (!get_id(r, v.id) || !get_gen(r, v.generation) || !get_enum(r, v.op_class) || !get_id(r, v.dataset) || !get_gen(r, v.dataset_generation) || !get_id(r, v.region) || !get_gen(r, v.region_generation) || !get_shape(r, v.shape) || !r.read_u64(v.input_bytes) || !r.read_u64(v.output_bytes) || !r.read_u64(v.workspace_bytes) || !get_enum(r, v.output_data_type) || !get_bool(r, v.determinism_required) || !r.read_u64(v.max_tolerated_latency_ns) || !r.read_u64(v.required_throughput_bps) || !get_enum(r, v.integrity) || !get_enum(r, v.retry_class) || !get_id(r, v.policy) || !get_gen(r, v.policy_generation) || !get_bool(r, has_prog) || !r.read_string(v.description)) return false; if (has_prog) { KernelOrProgramId pid; ProgramGeneration pg; if (!get_id(r, pid) || !get_gen(r, pg)) return false; v.program = pid; v.program_generation = pg; } st.operations[v.id] = v; }
  if (!r.read_u64(n) || n > kMaxPersistCount) return false;
  for (std::uint64_t i = 0; i < n; ++i) { TargetDefinition v; std::uint64_t nid=0,no=0,ndt=0,nl=0; if (!get_id(r, v.id) || !get_gen(r, v.generation) || !get_enum(r, v.provider) || !get_enum(r, v.kind) || !r.read_string(v.name) || !r.read_string(v.description) || !get_id(r, v.failure_domain) || !r.read_u64(nid)) return false; for(std::uint64_t j=0;j<nid && j<64;++j){ MemoryDomainId m; if(!get_id(r,m)) return false; v.memory_domains.push_back(m);} if(!r.read_u64(no)) return false; for(std::uint64_t j=0;j<no && j<64;++j){ OperationClass o; if(!get_enum(r,o)) return false; v.supported_ops.push_back(o);} if(!r.read_u64(ndt)) return false; for(std::uint64_t j=0;j<ndt && j<64;++j){ DataType d; if(!get_enum(r,d)) return false; v.supported_data_types.push_back(d);} if(!r.read_u64(nl)) return false; for(std::uint64_t j=0;j<nl && j<64;++j){ Layout l; if(!get_enum(r,l)) return false; v.supported_layouts.push_back(l);} std::uint32_t cc=0; if(!r.read_u64(v.max_input_bytes)||!r.read_u64(v.alignment_constraint)||!r.read_u32(cc)||!r.read_u64(v.queue_depth)||!r.read_u64(v.workspace_bytes)||!get_enum(r,v.access_semantics)||!get_bool(r,v.requires_staging)||!get_bool(r,v.synthetic)||!get_bool(r,v.supports_verification)||!get_bool(r,v.supports_cancellation)||!get_bool(r,v.supports_drain)||!r.read_string(v.authority_owner)) return false; v.concurrency_capacity = cc; st.targets[v.id] = v; }
  if (!r.read_u64(n) || n > kMaxPersistCount) return false;
  for (std::uint64_t i = 0; i < n; ++i) { ExecutionPlan v; bool hasd=false,hasa=false; std::uint64_t nnotes=0; if (!get_id(r, v.id) || !get_gen(r, v.generation) || !get_snapshot(r, v.authority.snapshot) || !get_id(r, v.target) || !get_gen(r, v.target_generation) || !get_id(r, v.operation) || !get_enum(r, v.op_class) || !get_id(r, v.dataset) || !get_gen(r, v.dataset_generation) || !get_id(r, v.region) || !get_gen(r, v.region_generation) || !get_enum(r, v.lifecycle) || !r.read_u64(v.reserved_slots) || !r.read_u64(v.reserved_workspace) || !get_bool(r, hasd)) return false; if (hasd) { DispatchId d; if (!get_id(r, d)) return false; v.dispatch_id = d; } if (!get_bool(r, hasa)) return false; if (hasa) { AttemptId a; if (!get_id(r, a)) return false; v.attempt_id = a; } if (!get_enum(r, v.retry_class) || !get_enum(r, v.decision) || !r.read_u64(nnotes)) return false; v.authority.plan = v.id; v.authority.plan_generation = v.generation; for(std::uint64_t j=0;j<nnotes && j<64;++j){ std::string s; if(!r.read_string(s)) return false; v.notes.push_back(s);} st.plans[v.id] = v; }
  if (!r.read_u64(n) || n > kMaxPersistCount) return false;
  for (std::uint64_t i = 0; i < n; ++i) { ExecutionResult v; if (!get_id(r, v.id) || !get_gen(r, v.generation) || !get_id(r, v.plan) || !get_gen(r, v.plan_generation) || !get_id(r, v.dispatch) || !get_id(r, v.attempt) || !get_id(r, v.target) || !get_gen(r, v.target_generation) || !get_id(r, v.worker_boot) || !get_gen(r, v.epoch) || !get_gen(r, v.dataset_generation) || !get_gen(r, v.region_generation) || !get_gen(r, v.operation_generation) || !get_enum(r, v.verification) || !r.read_string(v.output_digest) || !r.read_u64(v.output_bytes) || !get_bool(r, v.committed) || !get_bool(r, v.ambiguous) || !r.read_string(v.note)) return false; st.results[v.id] = v; }
  return true;
}

}  // namespace

Result<void> Coordinator::save(const std::filesystem::path& path) const {
  RuntimeState copy;
  {
    std::lock_guard<std::mutex> lk(mu_);
    copy = st_;
  }
  PersistentStore store;
  return store.save(path, [&copy](ByteWriter& w) { encode_state(copy, w); });
}

Result<void> Coordinator::load(const std::filesystem::path& path) {
  PersistentStore store;
  RuntimeState loaded;
  auto res = store.load(path, [&loaded](ByteReader& r) { return decode_state(loaded, r); });
  if (!res) return res.error();

  {
    std::lock_guard<std::mutex> lk(mu_);
    st_ = loaded;
    // Conservative recovery: advance the epoch and invalidate all dynamic authority.
    st_.epoch = CoordinatorEpoch(st_.epoch.value() + 1);
    for (auto& [tid, rt] : st_.target_runtime) { (void)tid; rt.dynamic_current = false; rt.reachable = false; rt.lifecycle = TargetLifecycle::REVALIDATION_REQUIRED; }
    for (auto& [tid, cs] : st_.capabilities) { (void)tid; cs.dynamic_current = false; }
    for (auto& [rid, ev] : st_.data_evidence) { (void)rid; ev.dynamic_current = false; }
    for (auto& [pid, plan] : st_.plans) {
      (void)pid;
      if (plan.lifecycle != ExecutionLifecycle::COMMITTED) {
        plan.lifecycle = (plan.lifecycle == ExecutionLifecycle::DISPATCHED ||
                          plan.lifecycle == ExecutionLifecycle::RUNNING ||
                          plan.lifecycle == ExecutionLifecycle::VERIFYING)
                             ? ExecutionLifecycle::OUTCOME_UNKNOWN
                             : ExecutionLifecycle::SUPERSEDED;
      }
    }
    rebuild_active_plans_locked();
    st_.reservations.clear();
    st_.workers.clear();
  }
  return Result<void>();
}

std::string Coordinator::inspect_text() const {
  std::lock_guard<std::mutex> lk(mu_);
  std::string out;
  out += "coordinator=" + std::to_string(st_.coordinator_id.value()) + " epoch=" + std::to_string(st_.epoch.value()) + "\n";
  out += "memory_domains=" + std::to_string(st_.memory_domains.size()) +
         " datasets=" + std::to_string(st_.datasets.size()) +
         " regions=" + std::to_string(st_.regions.size()) +
         " targets=" + std::to_string(st_.targets.size()) +
         " operations=" + std::to_string(st_.operations.size()) +
         " plans=" + std::to_string(st_.plans.size()) +
         " results=" + std::to_string(st_.results.size()) + "\n";
  for (const auto& [tid, def] : st_.targets) {
    const auto trt = st_.target_runtime.find(tid);
    TargetRuntime rt;
    if (trt != st_.target_runtime.end()) rt = trt->second;
    std::string cls;
    if (def.synthetic) cls = "SYNTHETIC";
    else if (rt.dynamic_current && rt.reachable) cls = "REAL";
    else if (def.kind == TargetKind::HOST_CPU_LOCAL) cls = "REAL";
    else cls = "UNKNOWN";
    out += "  target " + std::to_string(tid.value()) + " gen=" + std::to_string(def.generation.value()) +
           " kind=" + to_string(def.kind) + " provider=" + to_string(def.provider) + " class=" + cls +
           " lifecycle=" + to_string(rt.lifecycle) + " reachable=" + (rt.reachable ? "yes" : "no") + "\n";
  }
  return out;
}

}  // namespace nmc
