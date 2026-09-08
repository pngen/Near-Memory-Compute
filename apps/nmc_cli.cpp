#include "nmc/coordinator.hpp"
#include "nmc/system_discovery.hpp"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace nmc;

namespace {
std::map<std::string, std::string> parse(int argc, char** argv) {
  std::map<std::string, std::string> a;
  for (int i = 1; i < argc; ++i) {
    std::string s = argv[i];
    if (s.rfind("--", 0) == 0) {
      std::string k = s.substr(2);
      if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) a[k] = argv[++i];
      else a[k] = "1";
    }
  }
  return a;
}

// Build a Coordinator with one bounded synthetic target, a valid dataset/region, current data
// evidence, and a large operation so a near-memory plan can be produced.
void setup_demo(Coordinator& c, NearMemoryTargetId& target, DataRegionId& region, DatasetId& dataset, OperationId& op) {
  WorkerBootId boot = WorkerBootId::make();
  c.bind_worker_session(WorkerId::make(), boot, "demo-worker");
  MemoryDomainDescriptor md; md.id = MemoryDomainId::make(); md.generation = MemoryDomainGeneration(1); md.name = "md0";
  c.register_memory_domain(md);
  dataset = DatasetId::make();
  DatasetDescriptor ds; ds.id = dataset; ds.generation = DatasetGeneration(1); ds.name = "demo-ds"; ds.producer = "cli";
  c.register_dataset(ds);
  region = DataRegionId::make();
  DataRegionDescriptor rg; rg.id = region; rg.generation = DataRegionGeneration(1); rg.dataset = dataset;
  rg.dataset_generation = ds.generation; rg.memory_domain = md.id; rg.window = {0, 64};
  rg.shape = DataShape{4096, DataType::U8, Layout::CONTIGUOUS, 1}; rg.integrity = IntegrityState::INTACT;
  c.register_region(rg);
  target = NearMemoryTargetId::make();
  RegisterTargetMsg rt; rt.target = target; rt.generation = NearMemoryTargetGeneration(1);
  rt.provider = ProviderKind::SYNTHETIC; rt.kind = TargetKind::MEMORY_SIDE_PROCESSOR; rt.synthetic = true;
  rt.name = "syn-reduce"; rt.ops = {OperationClass::SUM, OperationClass::MIN, OperationClass::COUNT};
  rt.data_types = {DataType::U8}; rt.layouts = {Layout::CONTIGUOUS}; rt.max_input_bytes = 8ull << 30;
  rt.alignment = 1; rt.concurrency = 2; rt.workspace = 1 << 20; rt.owner_boot = boot;
  c.apply_register_target(rt);
  CapabilityMsg cap; cap.target = target; cap.generation = CapabilityGeneration(1);
  cap.evidence_generation = EvidenceGeneration(1); cap.publisher = boot;
  cap.entries = {{"operation:SUM", CapabilityState::SUPPORTED}, {"operation:MIN", CapabilityState::SUPPORTED},
                 {"operation:COUNT", CapabilityState::SUPPORTED}};
  c.apply_capability(cap);
  DataEvidenceMsg ev; ev.region = region; ev.region_generation = rg.generation; ev.dataset = dataset;
  ev.dataset_generation = ds.generation; ev.memory_domain = md.id; ev.memory_domain_generation = md.generation;
  ev.present = true; ev.current = true; ev.reachable = true; ev.publisher = boot;
  c.apply_data_evidence(ev);
  HeartbeatMsg hb; hb.target = target; hb.lifecycle = TargetLifecycle::READY; hb.reachable = true;
  hb.latency_ns = 12'000'000; hb.throughput_bps = 100ull << 30; hb.confidence = 0.95;
  c.apply_heartbeat(hb);
  OperationSpec o; o.id = OperationId::make(); o.generation = OperationGeneration(1);
  o.op_class = OperationClass::SUM; o.dataset = dataset; o.dataset_generation = ds.generation;
  o.region = region; o.region_generation = rg.generation;
  o.shape = DataShape{8ull << 30, DataType::U8, Layout::CONTIGUOUS, 1};
  o.input_bytes = 8ull << 30; o.output_bytes = 8; o.output_data_type = DataType::U64;
  o.retry_class = RetryClass::REPLAY_SAFE; o.integrity = IntegrityRequirement::CHECKED;
  o.policy = PolicyId(1); o.policy_generation = PolicyGeneration(1);
  c.create_operation(o);
  op = o.id;
}
}  // namespace

int main(int argc, char** argv) {
  std::string cmd = (argc > 1) ? argv[1] : "help";
  auto args = parse(argc, argv);

  if (cmd == "discover") {
    SystemDiscovery d = discover_system();
    std::printf("CPU %u logical / %u physical cores, %u NUMA nodes\n", d.cpu.logical_processors, d.cpu.physical_cores, d.cpu.numa_nodes);
    std::printf("Memory %llu MiB total / %llu MiB available\n", (unsigned long long)(d.memory.total_bytes >> 20), (unsigned long long)(d.memory.available_bytes >> 20));
    for (auto& g : d.gpus) std::printf("GPU: %s (dedicated %llu MiB)\n", g.name.c_str(), (unsigned long long)(g.dedicated_memory_bytes >> 20));
    std::printf("Physical near-memory compute hardware: %s\n", (d.near_memory.physical_pim || d.near_memory.physical_cxl_adjacent_compute || d.near_memory.physical_memory_side_processor || d.near_memory.physical_storage_side_compute || d.near_memory.physical_dpu_compute) ? "REAL" : "UNSUPPORTED (none present)");
    std::printf("Synthetic near-memory targets available: %s\n", d.near_memory.synthetic_targets_available ? "SYNTHETIC" : "UNKNOWN");
    return 0;
  }

  if (cmd == "help") {
    std::printf("nmc-cli commands:\n  discover  targets  operations  datasets  evaluate  plan  dispatch  results  drain  audit  inspect-state\n");
    return 0;
  }

  Coordinator c;
  NearMemoryTargetId target; DataRegionId region; DatasetId dataset; OperationId op;
  setup_demo(c, target, region, dataset, op);

  if (cmd == "targets" || cmd == "inspect-state") {
    std::printf("%s", c.inspect_text().c_str());
    return 0;
  }
  if (cmd == "operations") {
    auto st = c.state();
    std::printf("operations=%zu\n", st.operations.size());
    for (auto& [id, o] : st.operations)
      std::printf("  op %llu class=%s dataset=%llu region=%llu \n", (unsigned long long)id.value(), to_string(o.op_class), (unsigned long long)o.dataset.value(), (unsigned long long)o.region.value());
    return 0;
  }
  if (cmd == "datasets") {
    auto st = c.state();
    std::printf("datasets=%zu regions=%zu\n", st.datasets.size(), st.regions.size());
    return 0;
  }
  if (cmd == "evaluate" || cmd == "plan") {
    auto out = c.plan(op);
    if (!out) { std::printf("plan failed: %s\n", out.error().message.c_str()); return 1; }
    std::printf("decision=%s created=%d\n", to_string(out.value().decision), (int)out.value().created);
    for (auto& line : out.value().explanation) std::printf("  %s\n", line.c_str());
    return 0;
  }
  if (cmd == "dispatch") {
    auto out = c.plan(op);
    if (!out || !out.value().created) { std::printf("no near-memory plan to dispatch\n"); return 1; }
    ExecutionPlanId pid = out.value().plan.id;
    if (!c.reserve(pid)) { std::printf("reserve failed\n"); return 1; }
    DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
    auto dop = c.prepare_dispatch(pid, d, a);
    if (!dop) { std::printf("dispatch rejected (stale/unsupported): %s\n", dop.error().message.c_str()); return 1; }
    if (!c.confirm_dispatched(pid, d)) { std::printf("confirm failed\n"); return 1; }
    std::printf("dispatched plan %llu to worker; awaiting result\n", (unsigned long long)pid.value());
    return 0;
  }
  if (cmd == "results") {
    auto st = c.state();
    std::printf("results=%zu\n", st.results.size());
    for (auto& [rid, r] : st.results)
      std::printf("  result %llu plan=%llu committed=%d verification=%s\n", (unsigned long long)rid.value(), (unsigned long long)r.plan.value(), (int)r.committed, to_string(r.verification));
    return 0;
  }
  if (cmd == "drain") {
    auto r = c.begin_drain(target);
    std::printf("drain target %llu: %s\n", (unsigned long long)target.value(), r ? "accepted" : "failed");
    return r ? 0 : 1;
  }
  if (cmd == "audit") {
    std::printf("coordinator epoch=%llu targets=%zu plans=%zu results=%zu\n", (unsigned long long)c.current_epoch().value(), c.state().targets.size(), c.state().plans.size(), c.state().results.size());
    return 0;
  }

  std::printf("unknown command: %s\n", cmd.c_str());
  return 1;
}
