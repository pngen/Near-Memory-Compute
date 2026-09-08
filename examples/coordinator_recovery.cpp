#include "nmc/coordinator.hpp"
#include "examples/support.hpp"
#include <cstdio>

int main() {
  using namespace nmc;
    // save->load->conservative recovery: dynamic authority is NOT restored.
  std::filesystem::path p = std::filesystem::temp_directory_path() / "nmc_example_recovery.bin";
  std::filesystem::remove(p);
  nmc_example::Demo d;
  auto op = d.make_sum_op(8ull << 30);
  auto out = d.c.plan(op);
  if (out && out.value().created) {
    ExecutionPlanId pid = out.value().plan.id;
    d.c.reserve(pid);
    DispatchId disp = DispatchId::make(); AttemptId at = AttemptId::make();
    auto dop = d.c.prepare_dispatch(pid, disp, at);
    if (dop && d.c.confirm_dispatched(pid, disp)) {
      const auto& msg = dop.value().message;
      BackendRequest req{msg.op_class, msg.data_type, std::span<const std::uint8_t>(msg.payload.data(), msg.payload.size()), msg.payload.size()};
      auto rr = host_local_execute(req);
      ResultMsg rm;
      rm.plan = msg.plan; rm.plan_gen = msg.plan_gen; rm.dispatch = msg.dispatch; rm.attempt = msg.attempt;
      rm.target = msg.target; rm.target_gen = msg.target_gen; rm.worker_boot = msg.worker_boot; rm.epoch = msg.epoch;
      rm.dataset_gen = msg.dataset_gen; rm.region_gen = msg.region_gen; rm.operation_gen = msg.operation_gen;
      rm.result_gen = ResultGeneration(1); rm.output = rr.output; rm.output_digest = to_hex(rr.digest); rm.unknown = false;
      d.c.apply_result(rm);
    }
  }
  if (!d.c.save(p)) { std::printf("save failed\n"); return 1; }
  Coordinator c2;
  if (!c2.load(p)) { std::printf("load failed\n"); std::filesystem::remove(p); return 1; }
  std::printf("coordinator_recovery: recovered epoch=%llu has_target=%d (dynamic authority reset to REVALIDATION_REQUIRED)\n",
              (unsigned long long)c2.current_epoch().value(), (int)c2.has_target(d.target));
  std::filesystem::remove(p);
  return 0;
}
