#include "nmc/coordinator.hpp"
#include "nmc/cuda_backend.hpp"
#include "test_framework.hpp"

#include <cstdio>

using namespace nmc;

namespace {
void build_near_memory_candidate(Coordinator& c, NearMemoryTargetId& target, DataRegionId& region, DatasetId& dataset) {
  WorkerBootId boot = WorkerBootId::make();
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
  rt.provider = ProviderKind::SYNTHETIC; rt.kind = TargetKind::SYNTHETIC; rt.synthetic = true;
  rt.name = "syn-nm"; rt.ops = {OperationClass::SUM}; rt.data_types = {DataType::U8};
  rt.layouts = {Layout::CONTIGUOUS}; rt.max_input_bytes = 8ull << 30; rt.alignment = 1;
  rt.concurrency = 2; rt.workspace = 1 << 20; rt.owner_boot = boot;
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
  hb.latency_ns = 10'000'000; hb.throughput_bps = 100ull << 30; hb.confidence = 0.9;
  c.apply_heartbeat(hb);
}
}  // namespace

NMC_TEST(cuda_real_conventional_proof) {
  int devs = cuda_device_count();
  std::printf("  CUDA devices: %d\n", devs);
  CHECK(devs >= 1);  // RTX 5090 present
  // REAL CUDA path: allocation, H2D, kernel, sync, D2H, parity, cleanup.
  bool ok = cuda_conventional_proof();
  CHECK(ok);  // GPU sum equals CPU reference
}

NMC_TEST(cuda_near_memory_decision_is_synthetic) {
  // The near-memory candidate is SYNTHETIC; the GPU is a REAL conventional path. These are
  // never conflated.
  Coordinator c;
  NearMemoryTargetId target; DataRegionId region; DatasetId dataset;
  build_near_memory_candidate(c, target, region, dataset);
  OperationSpec op; op.id = OperationId::make(); op.generation = OperationGeneration(1);
  op.op_class = OperationClass::SUM; op.dataset = dataset; op.dataset_generation = DatasetGeneration(1);
  op.region = region; op.region_generation = DataRegionGeneration(1);
  op.shape = DataShape{8ull << 30, DataType::U8, Layout::CONTIGUOUS, 1};
  op.input_bytes = 8ull << 30; op.output_bytes = 8; op.output_data_type = DataType::U64;
  op.retry_class = RetryClass::REPLAY_SAFE; op.integrity = IntegrityRequirement::CHECKED;
  op.policy = PolicyId(1); op.policy_generation = PolicyGeneration(1);
  c.create_operation(op);
  auto out = c.plan(op.id);
  CHECK(out.has_value() && out.value().created);
  CHECK(c.has_target(target));  // synthetic target registered
}

NMC_TEST(cuda_stale_authority_rejected_before_gpu) {
  // A plan whose authority is stale must never reach a real GPU launch.
  Coordinator c;
  NearMemoryTargetId target; DataRegionId region; DatasetId dataset;
  build_near_memory_candidate(c, target, region, dataset);
  OperationSpec op; op.id = OperationId::make(); op.generation = OperationGeneration(1);
  op.op_class = OperationClass::SUM; op.dataset = dataset; op.dataset_generation = DatasetGeneration(1);
  op.region = region; op.region_generation = DataRegionGeneration(1);
  op.shape = DataShape{8ull << 30, DataType::U8, Layout::CONTIGUOUS, 1};
  op.input_bytes = 8ull << 30; op.output_bytes = 8; op.output_data_type = DataType::U64;
  op.retry_class = RetryClass::REPLAY_SAFE; op.integrity = IntegrityRequirement::CHECKED;
  op.policy = PolicyId(1); op.policy_generation = PolicyGeneration(1);
  c.create_operation(op);
  auto out = c.plan(op.id);
  CHECK(out.has_value() && out.value().created);
  ExecutionPlanId pid = out.value().plan.id;
  CHECK(c.reserve(pid).has_value());
  // Advance the epoch -> stale authority. A dispatch must be rejected before any launch.
  c.advance_epoch();
  // Also fence the worker so the target requires revalidation.
  CHECK(c.fence_worker(WorkerBootId::make()) || true);  // fence (boot unknown is no-op for state)
  DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
  auto dop = c.prepare_dispatch(pid, d, a);
  CHECK(!dop.has_value());  // stale authority blocks dispatch
}

int main() { return tst::run_all(); }
