#include "nmc/coordinator.hpp"
#include "examples/support.hpp"
#include <cstdio>

int main() {
  using namespace nmc;
    nmc_example::Demo d;
  for (auto bytes : { (std::uint64_t)(8ull << 30), (std::uint64_t)(1ull << 20) }) {
    auto op = d.make_sum_op(bytes);
    auto out = d.c.plan(op);
    if (out) std::printf("operation_selection: bytes=%llu decision=%s created=%d\n", (unsigned long long)bytes, to_string(out.value().decision), (int)out.value().created);
  }
  return 0;
}
