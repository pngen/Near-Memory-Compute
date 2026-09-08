#pragma once
// Shared helper for the examples: builds a Coordinator with one bounded synthetic near-memory
// target, current data evidence, and a large operation.
#include "nmc/coordinator.hpp"
#include <cstdio>

namespace nmc_example {

struct Demo {
  nmc::Coordinator c;
  nmc::NearMemoryTargetId target;
  nmc::DataRegionId region;
  nmc::DatasetId dataset;
  nmc::WorkerBootId boot;

  Demo() {
    boot = nmc::WorkerBootId::make();
    c.bind_worker_session(nmc::WorkerId::make(), boot, "worker");
    nmc::MemoryDomainDescriptor md; md.id = nmc::MemoryDomainId::make(); md.generation = nmc::MemoryDomainGeneration(1);
    c.register_memory_domain(md);
    dataset = nmc::DatasetId::make();
    nmc::DatasetDescriptor ds; ds.id = dataset; ds.generation = nmc::DatasetGeneration(1); ds.name = "ds";
    c.register_dataset(ds);
    region = nmc::DataRegionId::make();
    nmc::DataRegionDescriptor rg; rg.id = region; rg.generation = nmc::DataRegionGeneration(1);
    rg.dataset = dataset; rg.dataset_generation = ds.generation; rg.memory_domain = md.id;
    rg.window = {0, 64}; rg.shape = nmc::DataShape{4096, nmc::DataType::U8, nmc::Layout::CONTIGUOUS, 1};
    rg.integrity = nmc::IntegrityState::INTACT;
    c.register_region(rg);
    target = nmc::NearMemoryTargetId::make();
    nmc::RegisterTargetMsg rt; rt.target = target; rt.generation = nmc::NearMemoryTargetGeneration(1);
    rt.provider = nmc::ProviderKind::SYNTHETIC; rt.kind = nmc::TargetKind::MEMORY_SIDE_PROCESSOR; rt.synthetic = true;
    rt.name = "syn-reduce"; rt.ops = {nmc::OperationClass::SUM, nmc::OperationClass::MIN, nmc::OperationClass::COUNT};
    rt.data_types = {nmc::DataType::U8}; rt.layouts = {nmc::Layout::CONTIGUOUS}; rt.max_input_bytes = 8ull << 30;
    rt.alignment = 1; rt.concurrency = 2; rt.workspace = 1 << 20; rt.owner_boot = boot;
    c.apply_register_target(rt);
    nmc::CapabilityMsg cap; cap.target = target; cap.generation = nmc::CapabilityGeneration(1);
    cap.evidence_generation = nmc::EvidenceGeneration(1); cap.publisher = boot;
    cap.entries = {{"operation:SUM", nmc::CapabilityState::SUPPORTED}, {"operation:MIN", nmc::CapabilityState::SUPPORTED},
                   {"operation:COUNT", nmc::CapabilityState::SUPPORTED}};
    c.apply_capability(cap);
    nmc::DataEvidenceMsg ev; ev.region = region; ev.region_generation = rg.generation; ev.dataset = dataset;
    ev.dataset_generation = ds.generation; ev.memory_domain = md.id; ev.memory_domain_generation = md.generation;
    ev.present = true; ev.current = true; ev.reachable = true; ev.publisher = boot;
    c.apply_data_evidence(ev);
    nmc::HeartbeatMsg hb; hb.target = target; hb.lifecycle = nmc::TargetLifecycle::READY; hb.reachable = true;
    hb.latency_ns = 12'000'000; hb.throughput_bps = 100ull << 30; hb.confidence = 0.95;
    c.apply_heartbeat(hb);
  }

  nmc::OperationId make_sum_op(std::uint64_t bytes) {
    nmc::OperationSpec op; op.id = nmc::OperationId::make(); op.generation = nmc::OperationGeneration(1);
    op.op_class = nmc::OperationClass::SUM; op.dataset = dataset; op.dataset_generation = nmc::DatasetGeneration(1);
    op.region = region; op.region_generation = nmc::DataRegionGeneration(1);
    op.shape = nmc::DataShape{bytes, nmc::DataType::U8, nmc::Layout::CONTIGUOUS, 1};
    op.input_bytes = bytes; op.output_bytes = 8; op.output_data_type = nmc::DataType::U64;
    op.retry_class = nmc::RetryClass::REPLAY_SAFE; op.integrity = nmc::IntegrityRequirement::CHECKED;
    op.policy = nmc::PolicyId(1); op.policy_generation = nmc::PolicyGeneration(1);
    c.create_operation(op);
    return op.id;
  }
};

}  // namespace nmc_example
