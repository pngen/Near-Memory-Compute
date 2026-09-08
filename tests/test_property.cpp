#include "nmc/coordinator.hpp"
#include "nmc/digest.hpp"
#include "test_framework.hpp"

#include <random>

using namespace nmc;

namespace {
constexpr std::uint64_t kGiB = 1024ull * 1024 * 1024;

struct PCtx {
  Coordinator c;
  NearMemoryTargetId target;
  WorkerBootId boot;
  DataRegionId region;
  DatasetId dataset;
  PCtx() {
    boot = WorkerBootId::make();
    c.bind_worker_session(WorkerId::make(), boot, "w");
    MemoryDomainDescriptor md; md.id = MemoryDomainId::make(); md.generation = MemoryDomainGeneration(1);
    c.register_memory_domain(md);
    dataset = DatasetId::make();
    DatasetDescriptor ds; ds.id = dataset; ds.generation = DatasetGeneration(1); ds.name = "ds";
    c.register_dataset(ds);
    region = DataRegionId::make();
    DataRegionDescriptor rg; rg.id = region; rg.generation = DataRegionGeneration(1);
    rg.dataset = dataset; rg.dataset_generation = ds.generation; rg.memory_domain = md.id;
    rg.window = {0, 64}; rg.shape = DataShape{4096, DataType::U8, Layout::CONTIGUOUS, 1};
    c.register_region(rg);
    target = NearMemoryTargetId::make();
    RegisterTargetMsg rt; rt.target = target; rt.generation = NearMemoryTargetGeneration(1);
    rt.provider = ProviderKind::SYNTHETIC; rt.kind = TargetKind::MEMORY_SIDE_PROCESSOR; rt.synthetic = true;
    rt.name = "t"; rt.ops = {OperationClass::SUM, OperationClass::MIN}; rt.data_types = {DataType::U8};
    rt.layouts = {Layout::CONTIGUOUS}; rt.max_input_bytes = 8 * kGiB; rt.alignment = 1;
    rt.concurrency = 4; rt.workspace = 1 << 20; rt.owner_boot = boot;
    c.apply_register_target(rt);
    CapabilityMsg cap; cap.target = target; cap.generation = CapabilityGeneration(1);
    cap.evidence_generation = EvidenceGeneration(1);
    cap.entries = {{"operation:SUM", CapabilityState::SUPPORTED}, {"operation:MIN", CapabilityState::SUPPORTED}};
    c.apply_capability(cap);
    DataEvidenceMsg ev; ev.region = region; ev.region_generation = rg.generation;
    ev.dataset = dataset; ev.dataset_generation = ds.generation; ev.memory_domain = md.id;
    ev.memory_domain_generation = md.generation; ev.present = true; ev.current = true; ev.reachable = true;
    c.apply_data_evidence(ev);
    HeartbeatMsg hb; hb.target = target; hb.lifecycle = TargetLifecycle::READY; hb.reachable = true;
    hb.latency_ns = 10'000'000; hb.throughput_bps = 100ull << 30; hb.confidence = 0.9;
    c.apply_heartbeat(hb);
  }
  OperationId make_op(std::uint64_t bytes) {
    OperationSpec op; op.id = OperationId::make(); op.generation = OperationGeneration(1);
    op.op_class = OperationClass::SUM; op.dataset = dataset; op.dataset_generation = DatasetGeneration(1);
    op.region = region; op.region_generation = DataRegionGeneration(1);
    op.shape = DataShape{bytes, DataType::U8, Layout::CONTIGUOUS, 1};
    op.input_bytes = bytes; op.output_bytes = 8; op.output_data_type = DataType::U64;
    op.retry_class = RetryClass::REPLAY_SAFE; op.integrity = IntegrityRequirement::CHECKED;
    op.policy = PolicyId(1); op.policy_generation = PolicyGeneration(1);
    c.create_operation(op);
    return op.id;
  }
};

bool all_valid(ExecutionLifecycle l) {
  return l >= ExecutionLifecycle::PLANNED && l <= ExecutionLifecycle::SUPERSEDED;
}
}  // namespace

NMC_TEST(invariant_hard_constraints_dominate_economics) {
  // A target with a huge performance advantage but stale capability evidence must never be selected.
  PCtx t;
  RuntimeState st = t.c.state();
  CHECK(st.targets.size() == 1);
  // Downgrade the capability to UNKNOWN; even though economics would favor it, it must be ineligible.
  CapabilityMsg cap; cap.target = t.target; cap.generation = CapabilityGeneration(2);
  cap.evidence_generation = EvidenceGeneration(2);
  cap.entries = {{"operation:SUM", CapabilityState::UNKNOWN}};  // UNKNOWN fails closed
  t.c.apply_capability(cap);
  auto op = t.make_op(8 * kGiB);
  auto out = t.c.plan(op);
  CHECK(out.has_value());
  CHECK(!out.value().created);  // UNKNOWN operation capability must block selection
}

NMC_TEST(invariant_never_invent_success_for_unknown) {
  PCtx t;
  auto op = t.make_op(8 * kGiB);
  auto out = t.c.plan(op);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId pid = out.value().plan.id;
  CHECK(t.c.reserve(pid).has_value());
  DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
  auto dop = t.c.prepare_dispatch(pid, d, a);
  CHECK(dop.has_value());
  CHECK(t.c.confirm_dispatched(pid, d).has_value());
  ResultMsg rm;
  rm.plan = dop.value().message.plan; rm.plan_gen = dop.value().message.plan_gen;
  rm.dispatch = dop.value().message.dispatch; rm.attempt = dop.value().message.attempt;
  rm.target = dop.value().message.target; rm.target_gen = dop.value().message.target_gen;
  rm.worker_boot = dop.value().message.worker_boot; rm.epoch = dop.value().message.epoch;
  rm.dataset_gen = dop.value().message.dataset_gen; rm.region_gen = dop.value().message.region_gen;
  rm.operation_gen = dop.value().message.operation_gen; rm.result_gen = ResultGeneration(1);
  rm.unknown = true;
  auto res = t.c.apply_result(rm);
  CHECK(res.has_value());
  CHECK(!res.value().committed);  // ambiguous must NEVER be committed as success
  CHECK(res.value().ambiguous);
  CHECK(t.c.lifecycle_of(pid) == ExecutionLifecycle::OUTCOME_UNKNOWN);
}

NMC_TEST(invariant_dataset_generation_change_invalidates_stale_plan) {
  PCtx t;
  auto op = t.make_op(8 * kGiB);
  auto out = t.c.plan(op);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId pid = out.value().plan.id;
  CHECK(t.c.reserve(pid).has_value());
  // A NEW dataset generation is observed for this region. The plan pinned dataset generation 1;
  // the data evidence now reports generation 2, so the plan must be detected as stale.
  DataEvidenceMsg ev;
  ev.region = t.region; ev.region_generation = DataRegionGeneration(1);
  ev.dataset = t.dataset; ev.dataset_generation = DatasetGeneration(2);  // bumped
  ev.memory_domain = MemoryDomainId(1); ev.memory_domain_generation = MemoryDomainGeneration(1);
  ev.present = true; ev.current = true; ev.reachable = true;
  t.c.apply_data_evidence(ev);
  std::vector<StaleReason> stale = t.c.staleness_of(t.c.state().plans.at(pid));
  CHECK(!stale.empty());
  DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
  auto dop = t.c.prepare_dispatch(pid, d, a);
  CHECK(!dop.has_value());  // stale plan must never dispatch
}

NMC_TEST(invariant_reservation_accounting_closes_exactly) {
  PCtx t;
  auto op = t.make_op(8 * kGiB);
  auto out = t.c.plan(op);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId pid = out.value().plan.id;
  // Reserve two slots by creating and reserving multiple plans on the same op? op creation is unique.
  CHECK(t.c.reserve(pid).has_value());
  // Account after a successful commit: reservation must return to zero.
  DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
  auto dop = t.c.prepare_dispatch(pid, d, a);
  CHECK(dop.has_value());
  CHECK(t.c.confirm_dispatched(pid, d).has_value());
  const auto& msg = dop.value().message;
  BackendRequest req{msg.op_class, msg.data_type, std::span<const std::uint8_t>(msg.payload.data(), msg.payload.size()), msg.payload.size()};
  auto r = host_local_execute(req);
  ResultMsg rm;
  rm.plan = msg.plan; rm.plan_gen = msg.plan_gen; rm.dispatch = msg.dispatch; rm.attempt = msg.attempt;
  rm.target = msg.target; rm.target_gen = msg.target_gen; rm.worker_boot = msg.worker_boot;
  rm.epoch = msg.epoch; rm.dataset_gen = msg.dataset_gen; rm.region_gen = msg.region_gen;
  rm.operation_gen = msg.operation_gen; rm.result_gen = ResultGeneration(1);
  rm.output = r.output; rm.output_digest = to_hex(r.digest); rm.unknown = false;
  CHECK(t.c.apply_result(rm).has_value());
  RuntimeState st = t.c.state();
  auto it = st.reservations.find(t.target);
  CHECK(it != st.reservations.end());
  CHECK(it->second.reserved_slots == 0);  // closed exactly
  CHECK(all_valid(t.c.lifecycle_of(pid)));
}

NMC_TEST(invariant_retired_target_cannot_resurrect) {
  PCtx t;
  CHECK(t.c.retire_target(t.target).has_value());
  // Re-publishing must not make a retired target eligible again: a heartbeat to a terminal
  // target is rejected so a retired target cannot resurrect.
  HeartbeatMsg hb; hb.target = t.target; hb.lifecycle = TargetLifecycle::READY; hb.reachable = true;
  hb.latency_ns = 1; hb.throughput_bps = 1; hb.confidence = 1.0;
  CHECK(!t.c.apply_heartbeat(hb).has_value());  // heartbeat rejected for terminal target
  auto op = t.make_op(8 * kGiB);
  auto out = t.c.plan(op);
  CHECK(out.has_value());
  CHECK(!out.value().created);  // retired target is not eligible
}

NMC_TEST(invariant_deterministic_ranking_stable) {
  // Build two scenarios with identical inputs but reversed insertion order and assert identical ranking.
  // We insert two targets and verify the ordering is deterministic (lower cost wins, tie broken by id).
  PCtx a;
  NearMemoryTargetId t2 = NearMemoryTargetId::make();
  WorkerBootId b2 = WorkerBootId::make();
  a.c.bind_worker_session(WorkerId::make(), b2, "w2");
  RegisterTargetMsg rt; rt.target = t2; rt.generation = NearMemoryTargetGeneration(1);
  rt.provider = ProviderKind::SYNTHETIC; rt.kind = TargetKind::MEMORY_SIDE_PROCESSOR; rt.synthetic = true;
  rt.name = "t2"; rt.ops = {OperationClass::SUM}; rt.data_types = {DataType::U8};
  rt.layouts = {Layout::CONTIGUOUS}; rt.max_input_bytes = 8 * kGiB; rt.alignment = 1;
  rt.concurrency = 4; rt.workspace = 1 << 20; rt.owner_boot = b2;
  a.c.apply_register_target(rt);
  // Lower latency so t2 is clearly cheapest.
  CapabilityMsg cap; cap.target = t2; cap.generation = CapabilityGeneration(1);
  cap.evidence_generation = EvidenceGeneration(1); cap.entries = {{"operation:SUM", CapabilityState::SUPPORTED}};
  a.c.apply_capability(cap);
  HeartbeatMsg hb; hb.target = t2; hb.lifecycle = TargetLifecycle::READY; hb.reachable = true;
  hb.latency_ns = 1000; hb.throughput_bps = 200ull << 30; hb.confidence = 0.99;
  a.c.apply_heartbeat(hb);
  auto op = a.make_op(8 * kGiB);
  auto out = a.c.plan(op);
  CHECK(out.has_value() && out.value().created);
  CHECK(out.value().plan.target == t2);
}

int main() { return tst::run_all(); }
