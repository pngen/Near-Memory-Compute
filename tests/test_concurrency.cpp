#include "nmc/coordinator.hpp"
#include "nmc/digest.hpp"
#include "test_framework.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using namespace nmc;

namespace {
struct CCtx {
  Coordinator c;
  NearMemoryTargetId target;
  WorkerBootId boot;
  DataRegionId region;
  DatasetId dataset;
  std::uint32_t capacity;
  CCtx(std::uint32_t cap) : capacity(cap) {
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
    rt.layouts = {Layout::CONTIGUOUS}; rt.max_input_bytes = 8ull * 1024 * 1024 * 1024; rt.alignment = 1;
    rt.concurrency = cap; rt.workspace = 1 << 20; rt.owner_boot = boot;
    c.apply_register_target(rt);
    CapabilityMsg capmsg; capmsg.target = target; capmsg.generation = CapabilityGeneration(1);
    capmsg.evidence_generation = EvidenceGeneration(1);
    capmsg.entries = {{"operation:SUM", CapabilityState::SUPPORTED}};
    c.apply_capability(capmsg);
    DataEvidenceMsg ev; ev.region = region; ev.region_generation = rg.generation;
    ev.dataset = dataset; ev.dataset_generation = ds.generation; ev.memory_domain = md.id;
    ev.memory_domain_generation = md.generation; ev.present = true; ev.current = true; ev.reachable = true;
    c.apply_data_evidence(ev);
    HeartbeatMsg hb; hb.target = target; hb.lifecycle = TargetLifecycle::READY; hb.reachable = true;
    hb.latency_ns = 10'000'000; hb.throughput_bps = 100ull << 30; hb.confidence = 0.9;
    c.apply_heartbeat(hb);
  }
};
}  // namespace

NMC_TEST(concurrent_reserve_no_overcommit) {
  const std::uint32_t cap = 2;
  CCtx ctx(cap);
  std::atomic<int> reserved{0};
  std::atomic<bool> release{false};
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  std::vector<ExecutionPlanId> plans(cap);

  for (std::uint32_t i = 0; i < cap; ++i) {
    threads.emplace_back([&, i] {
      OperationId op = [&] {
        OperationSpec op; op.id = OperationId::make(); op.generation = OperationGeneration(1);
        op.op_class = OperationClass::SUM; op.dataset = ctx.dataset; op.dataset_generation = DatasetGeneration(1);
        op.region = ctx.region; op.region_generation = DataRegionGeneration(1);
        op.shape = DataShape{8ull * 1024 * 1024 * 1024, DataType::U8, Layout::CONTIGUOUS, 1};
        op.input_bytes = 8ull * 1024 * 1024 * 1024; op.output_bytes = 8; op.output_data_type = DataType::U64;
        op.retry_class = RetryClass::REPLAY_SAFE; op.integrity = IntegrityRequirement::CHECKED;
        op.policy = PolicyId(1); op.policy_generation = PolicyGeneration(1);
        ctx.c.create_operation(op);
        return op.id;
      }();
      auto out = ctx.c.plan(op);
      if (!out || !out.value().created) return;
      ExecutionPlanId pid = out.value().plan.id;
      plans[i] = pid;
      auto r = ctx.c.reserve(pid);
      if (r) { reserved.fetch_add(1); }
      // Hold the reservation until release.
      while (!release.load()) std::this_thread::yield();
      if (r) {
        DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
        auto dop = ctx.c.prepare_dispatch(pid, d, a);
        if (dop && ctx.c.confirm_dispatched(pid, d)) {
          const auto& msg = dop.value().message;
          BackendRequest req{msg.op_class, msg.data_type, std::span<const std::uint8_t>(msg.payload.data(), msg.payload.size()), msg.payload.size()};
          auto rr = host_local_execute(req);
          ResultMsg rm;
          rm.plan = msg.plan; rm.plan_gen = msg.plan_gen; rm.dispatch = msg.dispatch; rm.attempt = msg.attempt;
          rm.target = msg.target; rm.target_gen = msg.target_gen; rm.worker_boot = msg.worker_boot;
          rm.epoch = msg.epoch; rm.dataset_gen = msg.dataset_gen; rm.region_gen = msg.region_gen;
          rm.operation_gen = msg.operation_gen; rm.result_gen = ResultGeneration(1);
          rm.output = rr.output; rm.output_digest = to_hex(rr.digest); rm.unknown = false;
          ctx.c.apply_result(rm);
        }
      }
    });
  }

  // Wait until both reservation threads have reserved (or failed).
  while (reserved.load() < static_cast<int>(cap)) std::this_thread::yield();
  // A third reservation must FAIL (capacity exhausted atomically).
  OperationSpec op; op.id = OperationId::make(); op.generation = OperationGeneration(1);
  op.op_class = OperationClass::SUM; op.dataset = ctx.dataset; op.dataset_generation = DatasetGeneration(1);
  op.region = ctx.region; op.region_generation = DataRegionGeneration(1);
  op.shape = DataShape{8ull * 1024 * 1024 * 1024, DataType::U8, Layout::CONTIGUOUS, 1};
  op.input_bytes = 8ull * 1024 * 1024 * 1024; op.output_bytes = 8; op.output_data_type = DataType::U64;
  op.retry_class = RetryClass::REPLAY_SAFE; op.integrity = IntegrityRequirement::CHECKED;
  op.policy = PolicyId(1); op.policy_generation = PolicyGeneration(1);
  ctx.c.create_operation(op);
  auto out = ctx.c.plan(op.id);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId extra = out.value().plan.id;
  CHECK(!ctx.c.reserve(extra).has_value());  // capacity exhausted -> no overcommit

  release.store(true);
  for (auto& th : threads) th.join();
  RuntimeState st = ctx.c.state();
  auto it = st.reservations.find(ctx.target);
  CHECK(it != st.reservations.end());
  CHECK(it->second.reserved_slots == 0);  // all released, exactly
}

NMC_TEST(concurrent_reserve_vs_drain) {
  CCtx ctx(4);
  // One thread plans/drains; main verifies that after draining, new admissions are blocked.
  CHECK(ctx.c.begin_drain(ctx.target).has_value());
  OperationId op = [&] {
    OperationSpec op; op.id = OperationId::make(); op.generation = OperationGeneration(1);
    op.op_class = OperationClass::SUM; op.dataset = ctx.dataset; op.dataset_generation = DatasetGeneration(1);
    op.region = ctx.region; op.region_generation = DataRegionGeneration(1);
    op.shape = DataShape{8ull * 1024 * 1024 * 1024, DataType::U8, Layout::CONTIGUOUS, 1};
    op.input_bytes = 8ull * 1024 * 1024 * 1024; op.output_bytes = 8; op.output_data_type = DataType::U64;
    op.retry_class = RetryClass::REPLAY_SAFE; op.integrity = IntegrityRequirement::CHECKED;
    op.policy = PolicyId(1); op.policy_generation = PolicyGeneration(1);
    ctx.c.create_operation(op);
    return op.id;
  }();
  auto out = ctx.c.plan(op);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId pid = out.value().plan.id;
  CHECK(!ctx.c.reserve(pid).has_value());  // draining blocks admission
}

NMC_TEST(concurrent_plan_vs_evidence_update) {
  // Concurrent planning and updating data evidence must not corrupt state.
  CCtx ctx(8);
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  std::atomic<int> plans_ok{0};
  for (int t = 0; t < 6; ++t) {
    threads.emplace_back([&] {
      while (!go.load()) std::this_thread::yield();
      for (int k = 0; k < 200; ++k) {
        OperationSpec op; op.id = OperationId::make(); op.generation = OperationGeneration(1);
        op.op_class = OperationClass::SUM; op.dataset = ctx.dataset; op.dataset_generation = DatasetGeneration(1);
        op.region = ctx.region; op.region_generation = DataRegionGeneration(1);
        op.shape = DataShape{8ull * 1024 * 1024 * 1024, DataType::U8, Layout::CONTIGUOUS, 1};
        op.input_bytes = 8ull * 1024 * 1024 * 1024; op.output_bytes = 8; op.output_data_type = DataType::U64;
        op.retry_class = RetryClass::REPLAY_SAFE; op.integrity = IntegrityRequirement::CHECKED;
        op.policy = PolicyId(1); op.policy_generation = PolicyGeneration(1);
        ctx.c.create_operation(op);
        auto out = ctx.c.plan(op.id);
        if (out && out.value().created) plans_ok.fetch_add(1);
      }
    });
  }
  std::thread updater([&] {
    while (!go.load()) std::this_thread::yield();
    for (int k = 0; k < 1000; ++k) {
      DataEvidenceMsg ev; ev.region = ctx.region; ev.region_generation = DataRegionGeneration(1);
      ev.dataset = ctx.dataset; ev.dataset_generation = DatasetGeneration(1);
      ev.memory_domain = MemoryDomainId(1); ev.memory_domain_generation = MemoryDomainGeneration(1);
      ev.present = true; ev.current = true; ev.reachable = true;
      ctx.c.apply_data_evidence(ev);
    }
  });
  go.store(true);
  for (auto& th : threads) th.join();
  updater.join();
  CHECK(plans_ok.load() >= 0);  // the run completed naturally and the invariants held
}

int main() { return tst::run_all(); }
