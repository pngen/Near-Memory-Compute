#include "nmc/coordinator.hpp"
#include "examples/support.hpp"
#include <cstdio>

int main() {
  using namespace nmc;
    nmc_example::Demo d;
  auto op = d.make_sum_op(8ull << 30);
  auto out = d.c.plan(op);
  if (!out || !out.value().created) { std::printf("no plan\n"); return 1; }
  ExecutionPlanId pid = out.value().plan.id;
  d.c.reserve(pid);
  // Bump the observed dataset generation via a fresh data evidence publication.
  DataEvidenceMsg ev; ev.region = d.region; ev.region_generation = DataRegionGeneration(1);
  ev.dataset = d.dataset; ev.dataset_generation = DatasetGeneration(2);
  ev.memory_domain = MemoryDomainId(1); ev.memory_domain_generation = MemoryDomainGeneration(1);
  ev.present = true; ev.current = true; ev.reachable = true;
  d.c.apply_data_evidence(ev);
  auto stale = d.c.staleness_of(d.c.state().plans.at(pid));
  DispatchId disp = DispatchId::make(); AttemptId at = AttemptId::make();
  auto dop = d.c.prepare_dispatch(pid, disp, at);
  std::printf("stale_data_rejection: stale=%zu dispatch_ok=%d\n", stale.size(), (int)dop.has_value());
  return dop.has_value() ? 1 : 0;
}
