#include "nmc/coordinator.hpp"
#include "examples/support.hpp"
#include <cstdio>

int main() {
  using namespace nmc;
    // 8 GiB header moves cost more than near-memory execution; 4 MiB does not.
  nmc_example::Demo d;
  for (auto bytes : { (std::uint64_t)(8ull << 30), (std::uint64_t)(4ull << 20) }) {
    auto op = d.make_sum_op(bytes);
    auto out = d.c.plan(op);
    if (out) {
      std::printf("movement_vs_compute: bytes=%llu decision=%s\n", (unsigned long long)bytes, to_string(out.value().decision));
      for (auto& line : out.value().explanation) std::printf("    %s\n", line.c_str());
    }
  }
  return 0;
}
