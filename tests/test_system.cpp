#include "nmc/backend.hpp"
#include "nmc/system_discovery.hpp"
#include "test_framework.hpp"

#include <cstring>
#include <vector>

using namespace nmc;

NMC_TEST(discover_system_real_hardware) {
  SystemDiscovery d = discover_system();
  CHECK(d.cpu.logical_processors > 0);
  CHECK(d.cpu.physical_cores > 0);
  CHECK(d.cpu.numa_nodes >= 1);
  CHECK(d.memory.total_bytes > 0);
  CHECK(d.gpus.size() >= 1);
  // Absent physical near-memory hardware is reported as UNSUPPORTED, never inferred.
  CHECK(!d.near_memory.physical_pim);
  CHECK(!d.near_memory.physical_cxl_adjacent_compute);
  CHECK(!d.near_memory.physical_memory_side_processor);
  CHECK(!d.near_memory.physical_storage_side_compute);
  CHECK(!d.near_memory.physical_dpu_compute);
  // The runtime provides synthetic targets, not hardware.
  CHECK(d.near_memory.synthetic_targets_available);
  std::printf("  discovery summary: %s\n", d.summary.c_str());
}

NMC_TEST(host_local_real_execution_parity) {
  // Real host memory, deterministic data, real CPU reference.
  std::vector<std::uint8_t> buf(4096);
  std::uint64_t reference = 0;
  for (std::size_t i = 0; i < buf.size(); ++i) {
    buf[i] = static_cast<std::uint8_t>((i * 31 + 7) & 0xFF);
    reference += buf[i];
  }
  BackendRequest req{OperationClass::SUM, DataType::U8,
                     std::span<const std::uint8_t>(buf.data(), buf.size()), buf.size()};
  auto r = host_local_execute(req);
  CHECK(r.ok);
  CHECK(r.output.size() == 8);
  std::uint64_t got = 0;
  for (int i = 0; i < 8; ++i) got |= static_cast<std::uint64_t>(r.output[i]) << (i * 8);
  CHECK(got == reference);
  CHECK(r.execution_ns > 0);
}

NMC_TEST(host_local_empty_input_ok) {
  std::vector<std::uint8_t> buf;
  BackendRequest req{OperationClass::SUM, DataType::U8, std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(nullptr), 0), 0};
  auto r = host_local_execute(req);
  CHECK(r.ok);
  std::uint64_t got = 0;
  for (int i = 0; i < (int)r.output.size(); ++i) got |= static_cast<std::uint64_t>(r.output[i]) << (i * 8);
  CHECK(got == 0);
}

int main() { return tst::run_all(); }
