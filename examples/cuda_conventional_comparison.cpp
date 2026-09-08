#include "nmc/coordinator.hpp"
#include "nmc/system_discovery.hpp"
#include "examples/support.hpp"
#include <cstdio>

int main() {
  using namespace nmc;
  SystemDiscovery disc = discover_system();
  bool gpu = false;
  for (auto& g : disc.gpus) gpu = gpu || (g.name.find("RTX") != std::string::npos);
  // The near-memory candidate is SYNTHETIC; the CUDA GPU is a REAL conventional path. They are
  // never conflated. The runtime decides between near-memory and conventional deterministically.
  nmc_example::Demo d;
  auto op = d.make_sum_op(8ull << 30);
  auto out = d.c.plan(op);
  std::printf("cuda_conventional_comparison: real_gpu_present=%d near_memory_decision=%s (SYNTHETIC near-memory; REAL conventional GPU executed in the CUDA test)\n",
              (int)gpu, out ? to_string(out.value().decision) : "ERROR");
  return (out && out.value().decision == Decision::NEAR_MEMORY_SELECTED) ? 0 : 1;
}
