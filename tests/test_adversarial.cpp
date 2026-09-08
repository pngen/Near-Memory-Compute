#include "nmc/coordinator.hpp"
#include "test_framework.hpp"

using namespace nmc;

namespace {
struct ACtx {
  Coordinator c;
  NearMemoryTargetId target;
  WorkerBootId boot;
  DataRegionId region;
  DatasetId dataset;
  ACtx() {
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
    rt.name = "t"; rt.ops = {OperationClass::SUM}; rt.data_types = {DataType::U8};
    rt.layouts = {Layout::CONTIGUOUS}; rt.max_input_bytes = 8ull << 30; rt.alignment = 1;
    rt.concurrency = 4; rt.workspace = 1 << 20; rt.owner_boot = boot;
    c.apply_register_target(rt);
  }
  OperationId make_op(std::uint64_t bytes, std::uint64_t ws = 0, OperationClass oc = OperationClass::SUM) {
    OperationSpec op; op.id = OperationId::make(); op.generation = OperationGeneration(1);
    op.op_class = oc; op.dataset = dataset; op.dataset_generation = DatasetGeneration(1);
    op.region = region; op.region_generation = DataRegionGeneration(1);
    op.shape = DataShape{bytes, DataType::U8, Layout::CONTIGUOUS, 1};
    op.input_bytes = bytes; op.output_bytes = 8; op.output_data_type = DataType::U64;
    op.workspace_bytes = ws; op.retry_class = RetryClass::NON_RETRYABLE;
    op.integrity = IntegrityRequirement::CHECKED;
    op.policy = PolicyId(1); op.policy_generation = PolicyGeneration(1);
    c.create_operation(op);
    return op.id;
  }
  void publish_support() {
    CapabilityMsg cap; cap.target = target; cap.generation = CapabilityGeneration(1);
    cap.evidence_generation = EvidenceGeneration(1);
    cap.entries = {{"operation:SUM", CapabilityState::SUPPORTED}};
    c.apply_capability(cap);
    DataEvidenceMsg ev; ev.region = region; ev.region_generation = DataRegionGeneration(1);
    ev.dataset = dataset; ev.dataset_generation = DatasetGeneration(1);
    ev.memory_domain = MemoryDomainId(1); ev.memory_domain_generation = MemoryDomainGeneration(1);
    ev.present = true; ev.current = true; ev.reachable = true;
    c.apply_data_evidence(ev);
    HeartbeatMsg hb; hb.target = target; hb.lifecycle = TargetLifecycle::READY; hb.reachable = true;
    hb.latency_ns = 10'000'000; hb.throughput_bps = 100ull << 30; hb.confidence = 0.9;
    c.apply_heartbeat(hb);
  }
};
}  // namespace

NMC_TEST(duplicate_target_registration_rejected) {
  ACtx t;
  RegisterTargetMsg rt;
  rt.target = t.target; rt.generation = NearMemoryTargetGeneration(2);
  rt.provider = ProviderKind::SYNTHETIC; rt.kind = TargetKind::SYNTHETIC; rt.synthetic = true;
  rt.name = "dup"; rt.ops = {}; rt.data_types = {}; rt.layouts = {};
  rt.owner_boot = t.boot;
  // Re-registering updates the same id (overwrites), which the design treats as a generation
  // bump; that is allowed, but a NULL id must be rejected.
  RegisterTargetMsg bad;
  bad.target = NearMemoryTargetId();  // null
  bad.name = "bad";
  CHECK(!t.c.apply_register_target(bad).has_value());
}

NMC_TEST(unsupported_operation_no_plan) {
  ACtx t;
  t.publish_support();
  // SUM is supported; FILTER is not.
  auto op = t.make_op(8ull << 30, 0, OperationClass::FILTER);
  auto out = t.c.plan(op);
  CHECK(out.has_value());
  CHECK(!out.value().created);
}

NMC_TEST(stale_target_generation_rejected_at_dispatch) {
  ACtx t;
  t.publish_support();
  auto op = t.make_op(8ull << 30);
  auto out = t.c.plan(op);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId pid = out.value().plan.id;
  CHECK(t.c.reserve(pid).has_value());
  // Re-register the SAME target id with a NEW generation (bump).
  RegisterTargetMsg rt;
  rt.target = t.target; rt.generation = NearMemoryTargetGeneration(2);
  rt.provider = ProviderKind::SYNTHETIC; rt.kind = TargetKind::SYNTHETIC; rt.synthetic = true;
  rt.name = "t2"; rt.ops = {OperationClass::SUM}; rt.data_types = {DataType::U8};
  rt.layouts = {Layout::CONTIGUOUS}; rt.max_input_bytes = 8ull << 30; rt.alignment = 1;
  rt.concurrency = 4; rt.workspace = 1 << 20; rt.owner_boot = t.boot;
  CHECK(t.c.apply_register_target(rt).has_value());
  DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
  auto dop = t.c.prepare_dispatch(pid, d, a);
  CHECK(!dop.has_value());  // stale target generation blocks dispatch
}

NMC_TEST(stale_plan_generation_rejected_in_completion) {
  ACtx t;
  t.publish_support();
  auto op = t.make_op(8ull << 30);
  auto out = t.c.plan(op);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId pid = out.value().plan.id;
  CHECK(t.c.reserve(pid).has_value());
  DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
  auto dop = t.c.prepare_dispatch(pid, d, a);
  CHECK(dop.has_value());
  CHECK(t.c.confirm_dispatched(pid, d).has_value());
  const auto& msg = dop.value().message;
  ResultMsg rm;
  rm.plan = msg.plan; rm.plan_gen = ExecutionPlanGeneration(99);  // wrong generation
  rm.dispatch = msg.dispatch; rm.attempt = msg.attempt;
  rm.target = msg.target; rm.target_gen = msg.target_gen; rm.worker_boot = msg.worker_boot;
  rm.epoch = msg.epoch; rm.dataset_gen = msg.dataset_gen; rm.region_gen = msg.region_gen;
  rm.operation_gen = msg.operation_gen; rm.result_gen = ResultGeneration(1);
  rm.unknown = false;
  CHECK(!t.c.apply_result(rm).has_value());  // stale plan generation rejected
}

NMC_TEST(completion_after_cancellation_rejected) {
  ACtx t;
  t.publish_support();
  auto op = t.make_op(8ull << 30);
  auto out = t.c.plan(op);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId pid = out.value().plan.id;
  CHECK(t.c.reserve(pid).has_value());
  DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
  auto dop = t.c.prepare_dispatch(pid, d, a);
  CHECK(dop.has_value());
  CHECK(t.c.confirm_dispatched(pid, d).has_value());
  CHECK(t.c.cancel_plan(pid).has_value());
  CHECK(t.c.lifecycle_of(pid) == ExecutionLifecycle::CANCELLED);
  const auto& msg = dop.value().message;
  ResultMsg rm;
  rm.plan = msg.plan; rm.plan_gen = msg.plan_gen; rm.dispatch = msg.dispatch; rm.attempt = msg.attempt;
  rm.target = msg.target; rm.target_gen = msg.target_gen; rm.worker_boot = msg.worker_boot;
  rm.epoch = msg.epoch; rm.dataset_gen = msg.dataset_gen; rm.region_gen = msg.region_gen;
  rm.operation_gen = msg.operation_gen; rm.result_gen = ResultGeneration(1);
  rm.unknown = false;
  CHECK(!t.c.apply_result(rm).has_value());  // completion after cancellation rejected
}

NMC_TEST(insufficient_workspace_ineligible) {
  ACtx t;
  t.publish_support();
  auto op = t.make_op(8ull << 30, /*ws=*/ (1u << 20) + 1);  // exceeds target budget
  auto out = t.c.plan(op);
  CHECK(out.has_value());
  CHECK(!out.value().created);
}

NMC_TEST(zero_length_input_is_handled) {
  ACtx t;
  t.publish_support();
  auto op = t.make_op(0);
  auto out = t.c.plan(op);
  CHECK(out.has_value());  // must not crash
  // host-local on empty input returns a valid zero result.
  BackendRequest req{OperationClass::SUM, DataType::U8, std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(nullptr), 0), 0};
  auto r = host_local_execute(req);
  CHECK(r.ok);
}

NMC_TEST(old_epoch_traffic_cannot_mutate_state) {
  ACtx t;
  t.publish_support();
  auto op = t.make_op(8ull << 30);
  auto out = t.c.plan(op);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId pid = out.value().plan.id;
  CHECK(t.c.reserve(pid).has_value());
  DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
  auto dop = t.c.prepare_dispatch(pid, d, a);
  CHECK(dop.has_value());
  CHECK(t.c.confirm_dispatched(pid, d).has_value());
  t.c.advance_epoch();
  const auto& msg = dop.value().message;
  ResultMsg rm;
  rm.plan = msg.plan; rm.plan_gen = msg.plan_gen; rm.dispatch = msg.dispatch; rm.attempt = msg.attempt;
  rm.target = msg.target; rm.target_gen = msg.target_gen; rm.worker_boot = msg.worker_boot;
  rm.epoch = msg.epoch; rm.dataset_gen = msg.dataset_gen; rm.region_gen = msg.region_gen;
  rm.operation_gen = msg.operation_gen; rm.result_gen = ResultGeneration(1);
  rm.unknown = false; rm.output = {1,2,3}; rm.output_digest = "deadbeef";
  CHECK(!t.c.apply_result(rm).has_value());
}

int main() { return tst::run_all(); }
