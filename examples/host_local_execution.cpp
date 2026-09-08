#include "nmc/coordinator.hpp"
#include "examples/support.hpp"
#include <cstdio>

int main() {
  using namespace nmc;
    // REAL host-local execution on real host memory.
  std::vector<std::uint8_t> buf(4096);
  std::uint64_t ref = 0;
  for (std::size_t i = 0; i < buf.size(); ++i) { buf[i] = static_cast<std::uint8_t>((i * 17 + 3) & 0xFF); ref += buf[i]; }
  BackendRequest req{OperationClass::SUM, DataType::U8, std::span<const std::uint8_t>(buf.data(), buf.size()), buf.size()};
  auto r = host_local_execute(req);
  std::uint64_t got = 0;
  for (int i = 0; i < 8; ++i) got |= static_cast<std::uint64_t>(r.output[i]) << (i * 8);
  std::printf("host_local_execution: REAL host-local sum=%llu reference=%llu match=%d\n", (unsigned long long)got, (unsigned long long)ref, (int)(got == ref));
  return (got == ref) ? 0 : 1;
}
