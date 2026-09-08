#include "nmc/coordinator.hpp"
#include "examples/support.hpp"
#include <cstdio>

int main() {
  using namespace nmc;
    nmc_example::Demo d;
  auto op = d.make_sum_op(8ull << 30);
  auto out = d.c.plan(op);
  if (out && out.value().created) {
    d.c.fence_worker(d.boot);
    std::printf("target_failure: worker fenced; target lifecycle=%s\n", to_string(d.c.state().target_runtime.at(d.target).lifecycle));
  }
  return 0;
}
