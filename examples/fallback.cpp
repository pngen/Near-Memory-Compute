#include "nmc/coordinator.hpp"
#include "examples/support.hpp"
#include <cstdio>

int main() {
  using namespace nmc;
  nmc_example::Demo d;
  // A near-memory-required policy: if no eligible near-memory target exists, the runtime must
  // REJECT (never silently fall back).
  Policy pol;
  pol.id = PolicyId(100); pol.generation = PolicyGeneration(1);
  pol.name = "nm-required"; pol.outcome = PolicyOutcome::NEAR_MEMORY_REQUIRED;
  d.c.register_policy(pol);
  // Make the target ineligible (retired).
  d.c.retire_target(d.target);
  OperationSpec op; op.id = OperationId::make(); op.generation = OperationGeneration(1);
  op.op_class = OperationClass::SUM; op.dataset = d.dataset; op.dataset_generation = DatasetGeneration(1);
  op.region = d.region; op.region_generation = DataRegionGeneration(1);
  op.shape = DataShape{8ull << 30, DataType::U8, Layout::CONTIGUOUS, 1};
  op.input_bytes = 8ull << 30; op.output_bytes = 8; op.output_data_type = DataType::U64;
  op.retry_class = RetryClass::REPLAY_SAFE; op.integrity = IntegrityRequirement::CHECKED;
  op.policy = PolicyId(100); op.policy_generation = PolicyGeneration(1);
  d.c.create_operation(op);
  auto out = d.c.plan(op.id);
  std::string decision = out ? std::string(to_string(out.value().decision)) : "ERROR";
  std::printf("fallback: near-memory-required + no eligible target -> decision=%s (REJECT, no silent fallback)\n", decision.c_str());
  return (out && out.value().decision == Decision::REJECT) ? 0 : 1;
}
