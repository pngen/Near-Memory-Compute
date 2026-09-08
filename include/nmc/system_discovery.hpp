#pragma once
// Real OS/device discovery. Never infers near-memory hardware from RAM, CPU proximity, or NIC
// presence; absent hardware is reported as UNSUPPORTED, not inferred.

#include <cstdint>
#include <string>
#include <vector>

namespace nmc {

struct CpuInfo {
  std::string name;
  std::uint32_t logical_processors = 0;
  std::uint32_t physical_cores = 0;
  std::uint32_t numa_nodes = 0;
  std::uint64_t l2_cache_bytes = 0;
  std::uint64_t l3_cache_bytes = 0;
};

struct MemoryInfo {
  std::uint64_t total_bytes = 0;
  std::uint64_t available_bytes = 0;
};

struct GpuInfo {
  std::string name;
  bool present = false;
  std::uint64_t dedicated_memory_bytes = 0;
  bool cuda_capable = false;  // set only if a real CUDA runtime is loaded
};

struct NearMemoryHardwareInfo {
  bool physical_pim = false;                 // true ONLY if a real PIM device is present
  bool physical_cxl_adjacent_compute = false;
  bool physical_memory_side_processor = false;
  bool physical_storage_side_compute = false;
  bool physical_dpu_compute = false;
  bool synthetic_targets_available = false;  // runtime-provided, not hardware
};

struct SystemDiscovery {
  CpuInfo cpu;
  MemoryInfo memory;
  std::vector<GpuInfo> gpus;
  NearMemoryHardwareInfo near_memory;
  std::string summary;
};

// Discover using real OS/device APIs. Never throws; failures are recorded in the summary.
SystemDiscovery discover_system();

}  // namespace nmc
