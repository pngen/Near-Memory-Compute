#include "nmc/coordinator.hpp"
#include "examples/support.hpp"
#include <cstdio>

int main() {
  using namespace nmc;
    nmc_example::Demo d;
  std::printf("drain: begin_drain -> %d\n", (int)d.c.begin_drain(d.target).has_value());
  auto op = d.make_sum_op(8ull << 30);
  auto out = d.c.plan(op);
  if (out && out.value().created) {
    ExecutionPlanId pid = out.value().plan.id;
    std::printf("drain: reserve during drain -> ok=%d\n", (int)d.c.reserve(pid).has_value());
  }
  return 0;
}
