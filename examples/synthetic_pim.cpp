#include "nmc/coordinator.hpp"
#include "examples/support.hpp"
#include <cstdio>

int main() {
  using namespace nmc;
    nmc_example::Demo d;
  auto op = d.make_sum_op(8ull << 30);
  auto out = d.c.plan(op);
  bool created = out && out.value().created;
  std::printf("synthetic_pim: near-memory target kind=%s provider=%s class=SYNTHETIC plan_created=%d\n",
              to_string(d.c.state().targets.at(d.target).kind), to_string(d.c.state().targets.at(d.target).provider), (int)created);
  return 0;
}
