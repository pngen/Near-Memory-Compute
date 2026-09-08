#include "nmc/coordinator.hpp"
#include <chrono>
#include <cstdio>
#include <vector>

using namespace nmc;
using Clock = std::chrono::steady_clock;

namespace {
std::uint64_t ms_since(Clock::time_point a, Clock::time_point b) {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count());
}

void setup_target(Coordinator& c, NearMemoryTargetId& target, DataRegionId& region, DatasetId& dataset, WorkerBootId& boot) {
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
  rt.concurrency = 1ull << 20; rt.workspace = 1ull << 20; rt.owner_boot = boot;
  c.apply_register_target(rt);
  CapabilityMsg cap; cap.target = target; cap.generation = CapabilityGeneration(1);
  cap.evidence_generation = EvidenceGeneration(1);
  cap.entries = {{"operation:SUM", CapabilityState::SUPPORTED}};
  c.apply_capability(cap);
  DataEvidenceMsg ev; ev.region = region; ev.region_generation = DataRegionGeneration(1);
  ev.dataset = dataset; ev.dataset_generation = DatasetGeneration(1); ev.memory_domain = md.id;
  ev.memory_domain_generation = md.generation; ev.present = true; ev.current = true; ev.reachable = true;
  c.apply_data_evidence(ev);
  HeartbeatMsg hb; hb.target = target; hb.lifecycle = TargetLifecycle::READY; hb.reachable = true;
  hb.latency_ns = 12'000'000; hb.throughput_bps = 100ull << 30; hb.confidence = 0.95;
  c.apply_heartbeat(hb);
}
}  // namespace

int main() {
  std::printf("Near-Memory Compute benchmark (completed work)\n");
  std::printf("scale, plan_generation_ms, plans_created, reservation_ms, completion=(dispatch+commit)_ms, backend_execution_ms\n");

  for (std::size_t scale : {100u, 1000u, 10000u, 100000u}) {
    Coordinator c;
    NearMemoryTargetId target; DataRegionId region; DatasetId dataset; WorkerBootId boot;
    setup_target(c, target, region, dataset, boot);

    // Create and plan N operations; measure planner overhead separately from backend cost.
    std::vector<OperationId> ops; ops.reserve(scale);
    for (std::size_t i = 0; i < scale; ++i) {
      OperationSpec op; op.id = OperationId::make(); op.generation = OperationGeneration(1);
      op.op_class = OperationClass::SUM; op.dataset = dataset; op.dataset_generation = DatasetGeneration(1);
      op.region = region; op.region_generation = DataRegionGeneration(1);
      op.shape = DataShape{8ull << 30, DataType::U8, Layout::CONTIGUOUS, 1};
      op.input_bytes = 8ull << 30; op.output_bytes = 8; op.output_data_type = DataType::U64;
      op.retry_class = RetryClass::REPLAY_SAFE; op.integrity = IntegrityRequirement::CHECKED;
      op.policy = PolicyId(1); op.policy_generation = PolicyGeneration(1);
      c.create_operation(op);
      ops.push_back(op.id);
    }

    auto t0 = Clock::now();
    std::size_t created = 0;
    std::vector<ExecutionPlanId> plans; plans.reserve(scale);
    for (auto opid : ops) {
      auto out = c.plan(opid);
      if (out && out.value().created) { ++created; plans.push_back(out.value().plan.id); }
    }
    auto t1 = Clock::now();
    std::uint64_t plan_ms = ms_since(t0, t1);

    auto t2 = Clock::now();
    for (auto pid : plans) c.reserve(pid);
    auto t3 = Clock::now();
    std::uint64_t reserve_ms = ms_since(t2, t3);

    // Dispatch + commit each plan (in-process; the backend execution is timed separately).
    auto t4 = Clock::now();
    std::uint64_t backend_ms = 0;
    std::size_t committed = 0;
    for (auto pid : plans) {
      DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
      auto dop = c.prepare_dispatch(pid, d, a);
      if (!dop) continue;
      if (!c.confirm_dispatched(pid, d)) continue;
      const auto& msg = dop.value().message;
      BackendRequest req{msg.op_class, msg.data_type, std::span<const std::uint8_t>(msg.payload.data(), msg.payload.size()), msg.payload.size()};
      auto b0 = Clock::now();
      auto r = host_local_execute(req);
      auto b1 = Clock::now();
      backend_ms += ms_since(b0, b1);
      ResultMsg rm;
      rm.plan = msg.plan; rm.plan_gen = msg.plan_gen; rm.dispatch = msg.dispatch; rm.attempt = msg.attempt;
      rm.target = msg.target; rm.target_gen = msg.target_gen; rm.worker_boot = msg.worker_boot;
      rm.epoch = msg.epoch; rm.dataset_gen = msg.dataset_gen; rm.region_gen = msg.region_gen;
      rm.operation_gen = msg.operation_gen; rm.result_gen = ResultGeneration(1);
      rm.output = r.output; rm.output_digest = to_hex(r.digest); rm.unknown = false;
      if (c.apply_result(rm)) ++committed;
    }
    auto t5 = Clock::now();

    std::printf("%zu, %llu, %zu, %llu, (dispatch+commit)=%llu ms / %zu committed, backend_execution=%llu ms\n",
                scale, (unsigned long long)plan_ms, created, (unsigned long long)reserve_ms,
                (unsigned long long)ms_since(t4, t5), committed, (unsigned long long)backend_ms);
  }
  return 0;
}
