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
  if (!d.c.reserve(pid)) { std::printf("reserve failed\n"); return 1; }
  DispatchId disp = DispatchId::make(); AttemptId at = AttemptId::make();
  auto dop = d.c.prepare_dispatch(pid, disp, at);
  if (!dop) { std::printf("dispatch rejected: %s\n", dop.error().message.c_str()); return 1; }
  std::printf("basic_target: plan %llu reserved+dispatch-ready on target %llu (SYNTHETIC)\n",
              (unsigned long long)pid.value(), (unsigned long long)d.target.value());
  return 0;
}
