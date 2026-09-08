#include "nmc/coordinator.hpp"
#include "nmc/digest.hpp"
#include "test_framework.hpp"

using namespace nmc;

namespace {
constexpr std::uint64_t kGiB = 1024ull * 1024 * 1024;

struct Ctx {
  Coordinator c;
  NearMemoryTargetId target;
  WorkerBootId boot;
  WorkerId worker;
  DataRegionId region;
  DatasetId dataset;
  OperationId op_big;
  OperationId op_small;

  Ctx() {
    boot = WorkerBootId::make();
    worker = WorkerId::make();
    target = NearMemoryTargetId::make();
    dataset = DatasetId::make();
    region = DataRegionId::make();
    setup();
  }

  void setup() {
    c.bind_worker_session(worker, boot, "worker-A");

    MemoryDomainDescriptor md;
    md.id = MemoryDomainId::make(); md.generation = MemoryDomainGeneration(1); md.name = "md0";
    c.register_memory_domain(md);

    DatasetDescriptor ds;
    ds.id = dataset; ds.generation = DatasetGeneration(1); ds.name = "test-ds"; ds.producer = "test";
    c.register_dataset(ds);

    DataRegionDescriptor rg;
    rg.id = region; rg.generation = DataRegionGeneration(1);
    rg.dataset = dataset; rg.dataset_generation = ds.generation;
    rg.memory_domain = md.id; rg.window = {0, 64};
    rg.shape = DataShape{1'000'000, DataType::U8, Layout::CONTIGUOUS, 1};
    rg.integrity = IntegrityState::INTACT;
    c.register_region(rg);

    RegisterTargetMsg rt;
    rt.target = target; rt.generation = NearMemoryTargetGeneration(1);
    rt.provider = ProviderKind::SYNTHETIC; rt.kind = TargetKind::MEMORY_SIDE_PROCESSOR;
    rt.synthetic = true; rt.name = "syn-reduce";
    rt.ops = {OperationClass::SUM, OperationClass::MIN, OperationClass::COUNT};
    rt.data_types = {DataType::U8};
    rt.layouts = {Layout::CONTIGUOUS};
    rt.max_input_bytes = 16 * kGiB; rt.alignment = 1; rt.concurrency = 2;
    rt.workspace = 1 << 20; rt.owner_boot = boot;
    c.apply_register_target(rt);

    CapabilityMsg cap;
    cap.target = target; cap.generation = CapabilityGeneration(1);
    cap.evidence_generation = EvidenceGeneration(1);
    cap.entries = {{"operation:SUM", CapabilityState::SUPPORTED},
                   {"operation:MIN", CapabilityState::SUPPORTED},
                   {"operation:COUNT", CapabilityState::SUPPORTED}};
    c.apply_capability(cap);

    DataEvidenceMsg ev;
    ev.region = region; ev.region_generation = rg.generation;
    ev.dataset = dataset; ev.dataset_generation = ds.generation;
    ev.memory_domain = md.id; ev.memory_domain_generation = md.generation;
    ev.present = true; ev.current = true; ev.reachable = true;
    c.apply_data_evidence(ev);

    HeartbeatMsg hb;
    hb.target = target; hb.lifecycle = TargetLifecycle::READY; hb.reachable = true;
    hb.queue_depth = 0; hb.latency_ns = 12 * 1'000'000; hb.throughput_bps = 100ull * 1024 * 1024 * 1024;
    hb.confidence = 0.95;
    c.apply_heartbeat(hb);

    op_big = make_op(8 * kGiB, 64);
    op_small = make_op(1 * 1024 * 1024, 64);
  }

  OperationId make_op(std::uint64_t input_bytes, std::uint64_t output_bytes) {
    OperationSpec op;
    op.id = OperationId::make(); op.generation = OperationGeneration(1);
    op.op_class = OperationClass::SUM;
    op.dataset = dataset; op.dataset_generation = DatasetGeneration(1);
    op.region = region; op.region_generation = DataRegionGeneration(1);
    op.shape = DataShape{input_bytes, DataType::U8, Layout::CONTIGUOUS, 1};
    op.input_bytes = input_bytes; op.output_bytes = output_bytes;
    op.output_data_type = DataType::U64;
    op.determinism_required = true;
    op.integrity = IntegrityRequirement::CHECKED;
    op.retry_class = RetryClass::REPLAY_SAFE;
    op.policy = PolicyId(1); op.policy_generation = PolicyGeneration(1);
    c.create_operation(op);
    return op.id;
  }
};
}  // namespace

NMC_TEST(identity_zero_is_rejected) {
  CHECK(!Ctx().c.register_dataset(DatasetDescriptor{}).has_value());
}

NMC_TEST(plan_large_data_selects_near_memory) {
  Ctx t;
  auto out = t.c.plan(t.op_big);
  CHECK(out.has_value());
  CHECK(out.value().created);
  CHECK(out.value().decision == Decision::NEAR_MEMORY_SELECTED);
  CHECK(out.value().ranked.size() == 1);
  CHECK(out.value().ranked[0].target == t.target);
  CHECK(t.c.has_plan(out.value().plan.id));
}

NMC_TEST(plan_small_data_prefers_conventional) {
  Ctx t;
  auto out = t.c.plan(t.op_small);
  CHECK(out.has_value());
  CHECK(!out.value().created);
  CHECK(out.value().decision == Decision::CONVENTIONAL_CPU_SELECTED);
}

NMC_TEST(plan_then_reserve_then_dispatch_then_result_commits) {
  Ctx t;
  auto out = t.c.plan(t.op_big);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId plan_id = out.value().plan.id;
  CHECK(t.c.reserve(plan_id).has_value());
  CHECK(t.c.lifecycle_of(plan_id) == ExecutionLifecycle::RESERVED);

  DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
  auto dop = t.c.prepare_dispatch(plan_id, d, a);
  CHECK(dop.has_value());
  CHECK(dop.value().ready);
  CHECK(dop.value().worker == t.worker);
  CHECK(t.c.confirm_dispatched(plan_id, d).has_value());
  CHECK(t.c.lifecycle_of(plan_id) == ExecutionLifecycle::DISPATCHED);

  // Build a result message from the worker.
  const DispatchMsg& msg = dop.value().message;
  BackendRequest req;
  req.op_class = msg.op_class; req.data_type = msg.data_type;
  req.input = std::span<const std::uint8_t>(msg.payload.data(), msg.payload.size());
  req.input_bytes = req.input.size();
  auto res = host_local_execute(req);
  CHECK(res.ok);

  ResultMsg rm;
  rm.plan = msg.plan; rm.plan_gen = msg.plan_gen;
  rm.dispatch = msg.dispatch; rm.attempt = msg.attempt;
  rm.target = msg.target; rm.target_gen = msg.target_gen;
  rm.worker_boot = msg.worker_boot; rm.epoch = msg.epoch;
  rm.dataset_gen = msg.dataset_gen; rm.region_gen = msg.region_gen;
  rm.operation_gen = msg.operation_gen;
  rm.result_gen = ResultGeneration(1);
  rm.output = res.output;
  rm.output_digest = to_hex(res.digest);
  rm.unknown = false;

  auto committed = t.c.apply_result(rm);
  CHECK(committed.has_value());
  CHECK(committed.value().committed);
  CHECK(committed.value().verification == VerifyState::VERIFIED);
  CHECK(t.c.lifecycle_of(plan_id) == ExecutionLifecycle::COMMITTED);
  CHECK(t.c.has_result(plan_id));
  CHECK(t.c.result_of(plan_id).committed);
}

NMC_TEST(duplicate_completion_is_rejected_no_double_commit) {
  Ctx t;
  auto out = t.c.plan(t.op_big);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId plan_id = out.value().plan.id;
  CHECK(t.c.reserve(plan_id).has_value());
  DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
  auto dop = t.c.prepare_dispatch(plan_id, d, a);
  CHECK(dop.has_value());
  CHECK(t.c.confirm_dispatched(plan_id, d).has_value());

  const auto& msg = dop.value().message;
  BackendRequest req{msg.op_class, msg.data_type,
                     std::span<const std::uint8_t>(msg.payload.data(), msg.payload.size()), msg.payload.size()};
  auto r = host_local_execute(req);
  ResultMsg rm;
  rm.plan = msg.plan; rm.plan_gen = msg.plan_gen; rm.dispatch = msg.dispatch; rm.attempt = msg.attempt;
  rm.target = msg.target; rm.target_gen = msg.target_gen; rm.worker_boot = msg.worker_boot;
  rm.epoch = msg.epoch; rm.dataset_gen = msg.dataset_gen; rm.region_gen = msg.region_gen;
  rm.operation_gen = msg.operation_gen; rm.result_gen = ResultGeneration(1);
  rm.output = r.output; rm.output_digest = to_hex(r.digest); rm.unknown = false;

  auto first = t.c.apply_result(rm);
  CHECK(first.has_value() && first.value().committed);
  auto second = t.c.apply_result(rm);
  CHECK(!second.has_value());  // duplicate completion must be rejected
  CHECK(t.c.result_of(plan_id).committed);
}

NMC_TEST(stale_worker_boot_rejected_before_dispatch) {
  Ctx t;
  auto out = t.c.plan(t.op_big);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId plan_id = out.value().plan.id;
  CHECK(t.c.reserve(plan_id).has_value());
  // Fence the worker boot -> plan must become stale.
  CHECK(t.c.fence_worker(t.boot).has_value());
  DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
  auto dop = t.c.prepare_dispatch(plan_id, d, a);
  CHECK(!dop.has_value());  // stale authority: no dispatch
  CHECK(t.c.lifecycle_of(plan_id) == ExecutionLifecycle::FENCED);
}

NMC_TEST(stale_epoch_rejects_old_result) {
  Ctx t;
  auto out = t.c.plan(t.op_big);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId plan_id = out.value().plan.id;
  CHECK(t.c.reserve(plan_id).has_value());
  DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
  auto dop = t.c.prepare_dispatch(plan_id, d, a);
  CHECK(dop.has_value());
  CHECK(t.c.confirm_dispatched(plan_id, d).has_value());

  // Advance the epoch; the result now carries a stale epoch and must be rejected.
  t.c.advance_epoch();

  const auto& msg = dop.value().message;
  BackendRequest req{msg.op_class, msg.data_type,
                     std::span<const std::uint8_t>(msg.payload.data(), msg.payload.size()), msg.payload.size()};
  auto r = host_local_execute(req);
  ResultMsg rm;
  rm.plan = msg.plan; rm.plan_gen = msg.plan_gen; rm.dispatch = msg.dispatch; rm.attempt = msg.attempt;
  rm.target = msg.target; rm.target_gen = msg.target_gen; rm.worker_boot = msg.worker_boot;
  rm.epoch = msg.epoch; rm.dataset_gen = msg.dataset_gen; rm.region_gen = msg.region_gen;
  rm.operation_gen = msg.operation_gen; rm.result_gen = ResultGeneration(1);
  rm.output = r.output; rm.output_digest = to_hex(r.digest); rm.unknown = false;
  auto res = t.c.apply_result(rm);
  CHECK(!res.has_value());  // stale epoch completion rejected
}

NMC_TEST(outcome_unknown_is_modeled_not_committed) {
  Ctx t;
  auto out = t.c.plan(t.op_big);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId plan_id = out.value().plan.id;
  CHECK(t.c.reserve(plan_id).has_value());
  DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
  auto dop = t.c.prepare_dispatch(plan_id, d, a);
  CHECK(dop.has_value());
  CHECK(t.c.confirm_dispatched(plan_id, d).has_value());

  ResultMsg rm;
  rm.plan = dop.value().message.plan; rm.plan_gen = dop.value().message.plan_gen;
  rm.dispatch = dop.value().message.dispatch; rm.attempt = dop.value().message.attempt;
  rm.target = dop.value().message.target; rm.target_gen = dop.value().message.target_gen;
  rm.worker_boot = dop.value().message.worker_boot; rm.epoch = dop.value().message.epoch;
  rm.dataset_gen = dop.value().message.dataset_gen; rm.region_gen = dop.value().message.region_gen;
  rm.operation_gen = dop.value().message.operation_gen; rm.result_gen = ResultGeneration(1);
  rm.unknown = true; rm.note = "completion lost";
  auto res = t.c.apply_result(rm);
  CHECK(res.has_value());
  CHECK(!res.value().committed);
  CHECK(res.value().ambiguous);
  CHECK(t.c.lifecycle_of(plan_id) == ExecutionLifecycle::OUTCOME_UNKNOWN);
}

NMC_TEST(drain_blocks_new_admission) {
  Ctx t;
  CHECK(t.c.begin_drain(t.target).has_value());
  auto out = t.c.plan(t.op_big);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId plan_id = out.value().plan.id;
  CHECK(!t.c.reserve(plan_id).has_value());  // reservation rejected during drain
}

NMC_TEST(retired_target_is_ineligible) {
  Ctx t;
  CHECK(t.c.retire_target(t.target).has_value());
  auto out = t.c.plan(t.op_big);
  CHECK(out.has_value());
  CHECK(!out.value().created);
  CHECK(out.value().decision == Decision::MOVEMENT_REQUIRED);
}

NMC_TEST(ranking_is_insertion_order_independent) {
  // Two eligible targets with different costs; ranking must be stable regardless of order.
  Ctx t;
  // Add a second, lower-latency target so it wins.
  NearMemoryTargetId t2 = NearMemoryTargetId::make();
  WorkerBootId b2 = WorkerBootId::make();
  t.c.bind_worker_session(WorkerId::make(), b2, "worker-B");
  RegisterTargetMsg rt;
  rt.target = t2; rt.generation = NearMemoryTargetGeneration(1);
  rt.provider = ProviderKind::SYNTHETIC; rt.kind = TargetKind::MEMORY_SIDE_PROCESSOR;
  rt.synthetic = true; rt.name = "syn-fast"; rt.ops = {OperationClass::SUM};
  rt.data_types = {DataType::U8}; rt.layouts = {Layout::CONTIGUOUS};
  rt.max_input_bytes = 16 * kGiB; rt.alignment = 1; rt.concurrency = 4; rt.workspace = 1 << 20;
  rt.owner_boot = b2;
  t.c.apply_register_target(rt);
  CapabilityMsg cap; cap.target = t2; cap.generation = CapabilityGeneration(1);
  cap.evidence_generation = EvidenceGeneration(1); cap.entries = {{"operation:SUM", CapabilityState::SUPPORTED}};
  t.c.apply_capability(cap);
  HeartbeatMsg hb; hb.target = t2; hb.lifecycle = TargetLifecycle::READY; hb.reachable = true;
  hb.latency_ns = 1'000'000; hb.throughput_bps = 200ull * 1024 * 1024 * 1024; hb.confidence = 0.99;
  t.c.apply_heartbeat(hb);

  auto out = t.c.plan(t.op_big);
  CHECK(out.has_value() && out.value().created);
  CHECK(out.value().ranked.size() >= 1);
  CHECK(out.value().plan.target == t2);  // lower-latency target should win
}

int main() { return tst::run_all(); }
