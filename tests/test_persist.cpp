#include "nmc/coordinator.hpp"
#include "nmc/persistence.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>

using namespace nmc;

namespace {
const std::filesystem::path kTmp = std::filesystem::temp_directory_path() / "nmc_persist_test.bin";

void build_committed(Coordinator& c, NearMemoryTargetId& target, ExecutionPlanId& plan_id) {
  WorkerBootId boot = WorkerBootId::make();
  c.bind_worker_session(WorkerId::make(), boot, "w");
  MemoryDomainDescriptor md; md.id = MemoryDomainId::make(); md.generation = MemoryDomainGeneration(1);
  c.register_memory_domain(md);
  DatasetDescriptor ds; ds.id = DatasetId::make(); ds.generation = DatasetGeneration(1); ds.name = "ds";
  c.register_dataset(ds);
  DataRegionDescriptor rg; rg.id = DataRegionId::make(); rg.generation = DataRegionGeneration(1);
  rg.dataset = ds.id; rg.dataset_generation = ds.generation; rg.memory_domain = md.id;
  rg.window = {0, 64}; rg.shape = DataShape{4096, DataType::U8, Layout::CONTIGUOUS, 1};
  c.register_region(rg);

  target = NearMemoryTargetId::make();
  RegisterTargetMsg rt; rt.target = target; rt.generation = NearMemoryTargetGeneration(1);
  rt.provider = ProviderKind::SYNTHETIC; rt.kind = TargetKind::MEMORY_SIDE_PROCESSOR; rt.synthetic = true;
  rt.name = "t"; rt.ops = {OperationClass::SUM}; rt.data_types = {DataType::U8};
  rt.layouts = {Layout::CONTIGUOUS}; rt.max_input_bytes = 1u<<30; rt.alignment = 1;
  rt.concurrency = 2; rt.workspace = 1u<<20; rt.owner_boot = boot;
  c.apply_register_target(rt);
  CapabilityMsg cap; cap.target = target; cap.generation = CapabilityGeneration(1);
  cap.evidence_generation = EvidenceGeneration(1);
  cap.entries = {{"operation:SUM", CapabilityState::SUPPORTED}};
  c.apply_capability(cap);
  DataEvidenceMsg ev; ev.region = rg.id; ev.region_generation = rg.generation;
  ev.dataset = ds.id; ev.dataset_generation = ds.generation; ev.memory_domain = md.id;
  ev.memory_domain_generation = md.generation; ev.present = true; ev.current = true; ev.reachable = true;
  c.apply_data_evidence(ev);
  HeartbeatMsg hb; hb.target = target; hb.lifecycle = TargetLifecycle::READY; hb.reachable = true;
  hb.latency_ns = 10'000'000; hb.throughput_bps = 100ull<<30; hb.confidence = 0.9;
  c.apply_heartbeat(hb);

  OperationSpec op; op.id = OperationId::make(); op.generation = OperationGeneration(1);
  op.op_class = OperationClass::SUM; op.dataset = ds.id; op.dataset_generation = ds.generation;
  op.region = rg.id; op.region_generation = rg.generation;
  op.shape = DataShape{8ull * 1024 * 1024 * 1024, DataType::U8, Layout::CONTIGUOUS, 1};
  op.input_bytes = 8ull * 1024 * 1024 * 1024; op.output_bytes = 8; op.output_data_type = DataType::U64;
  op.retry_class = RetryClass::REPLAY_SAFE; op.integrity = IntegrityRequirement::CHECKED;
  op.policy = PolicyId(1); op.policy_generation = PolicyGeneration(1);
  c.create_operation(op);
  auto out = c.plan(op.id);
  if (!out || !out.value().created) { plan_id = ExecutionPlanId(); return; }
  plan_id = out.value().plan.id;
  if (!c.reserve(plan_id)) return;
  DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
  auto dop = c.prepare_dispatch(plan_id, d, a);
  if (!dop) return;
  if (!c.confirm_dispatched(plan_id, d)) return;
  const auto& msg = dop.value().message;
  BackendRequest req{msg.op_class, msg.data_type, std::span<const std::uint8_t>(msg.payload.data(), msg.payload.size()), msg.payload.size()};
  auto r = host_local_execute(req);
  ResultMsg rm;
  rm.plan = msg.plan; rm.plan_gen = msg.plan_gen; rm.dispatch = msg.dispatch; rm.attempt = msg.attempt;
  rm.target = msg.target; rm.target_gen = msg.target_gen; rm.worker_boot = msg.worker_boot;
  rm.epoch = msg.epoch; rm.dataset_gen = msg.dataset_gen; rm.region_gen = msg.region_gen;
  rm.operation_gen = msg.operation_gen; rm.result_gen = ResultGeneration(1);
  rm.output = r.output; rm.output_digest = to_hex(r.digest); rm.unknown = false;
  c.apply_result(rm);
}
}  // namespace

NMC_TEST(persist_save_load_recovery) {
  std::filesystem::remove(kTmp);
  Coordinator c;
  NearMemoryTargetId target; ExecutionPlanId plan_id;
  build_committed(c, target, plan_id);
  CoordinatorEpoch old_epoch = c.current_epoch();
  CHECK(c.save(kTmp).has_value());

  Coordinator c2;
  auto load_res = c2.load(kTmp);
  if (!load_res) std::printf("  [persist-debug] load error code=%s msg=%s\n", load_res.error().code.c_str(), load_res.error().message.c_str());
  CHECK(load_res.has_value());
  CHECK(c2.current_epoch().value() > old_epoch.value());  // epoch advanced
  CHECK(c2.has_target(target));                            // durable target survives
  CHECK(c2.has_plan(plan_id));                             // durable plan history survives
  CHECK(c2.has_result(plan_id));                           // committed result survives
  // Dynamic runtime authority must NOT survive: a fresh plan requires revalidation and thus
  // yields no near-memory plan until evidence is re-published.
  std::filesystem::remove(kTmp);
}

NMC_TEST(persist_corruption_rejected) {
  std::filesystem::remove(kTmp);
  Coordinator c;
  NearMemoryTargetId t; ExecutionPlanId p;
  build_committed(c, t, p);
  CHECK(c.save(kTmp).has_value());

  // Read the file, corrupt one byte in the middle of the payload.
  std::vector<char> raw;
  { std::ifstream f(kTmp, std::ios::binary); raw.assign(std::istreambuf_iterator<char>(f), {}); }
  CHECK(raw.size() > 12);
  raw[raw.size() / 2] ^= 0x5A;
  { std::ofstream f(kTmp, std::ios::binary | std::ios::trunc); f.write(raw.data(), raw.size()); }

  Coordinator c2;
  CHECK(!c2.load(kTmp).has_value());  // corruption must be rejected
  std::filesystem::remove(kTmp);
}

NMC_TEST(persist_truncation_rejected) {
  std::filesystem::remove(kTmp);
  Coordinator c;
  NearMemoryTargetId t; ExecutionPlanId p;
  build_committed(c, t, p);
  CHECK(c.save(kTmp).has_value());
  std::vector<char> raw;
  { std::ifstream f(kTmp, std::ios::binary); raw.assign(std::istreambuf_iterator<char>(f), {}); }
  CHECK(raw.size() > 4);
  raw.resize(raw.size() - 7);  // truncate the trailing checksum + some payload
  { std::ofstream f(kTmp, std::ios::binary | std::ios::trunc); f.write(raw.data(), raw.size()); }
  Coordinator c2;
  CHECK(!c2.load(kTmp).has_value());
  std::filesystem::remove(kTmp);
}

NMC_TEST(persist_trailing_garbage_rejected) {
  std::filesystem::remove(kTmp);
  Coordinator c;
  NearMemoryTargetId t; ExecutionPlanId p;
  build_committed(c, t, p);
  CHECK(c.save(kTmp).has_value());
  std::vector<char> raw;
  { std::ifstream f(kTmp, std::ios::binary); raw.assign(std::istreambuf_iterator<char>(f), {}); }
  raw.push_back('X'); raw.push_back('\n');  // trailing garbage
  { std::ofstream f(kTmp, std::ios::binary | std::ios::trunc); f.write(raw.data(), raw.size()); }
  Coordinator c2;
  CHECK(!c2.load(kTmp).has_value());
  std::filesystem::remove(kTmp);
}

NMC_TEST(persist_bad_magic_rejected) {
  std::filesystem::remove(kTmp);
  { std::ofstream f(kTmp, std::ios::binary | std::ios::trunc); f << "NOTAMAGICFILE" << "x"; }
  Coordinator c;
  CHECK(!c.load(kTmp).has_value());
  std::filesystem::remove(kTmp);
}

int main() { return tst::run_all(); }
