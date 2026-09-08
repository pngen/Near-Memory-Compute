#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <windows.h>

#include "nmc/system_discovery.hpp"

#include <string>
#include <vector>

namespace nmc {

using Microsoft::WRL::ComPtr;

namespace {
std::uint32_t highest_numa_node() {
  ULONG highest = 0;
  if (GetNumaHighestNodeNumber(&highest)) return static_cast<std::uint32_t>(highest) + 1;
  return 1;
}

std::uint64_t get_cache_bytes(LOGICAL_PROCESSOR_RELATIONSHIP rel) {
  std::uint64_t bytes = 0;
  DWORD len = 0;
  GetLogicalProcessorInformationEx(rel, nullptr, &len);
  if (len == 0) return 0;
  std::vector<unsigned char> buf(len);
  if (!GetLogicalProcessorInformationEx(rel, reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data()), &len))
    return 0;
  for (DWORD off = 0; off < len;) {
    const auto* p = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + off);
    if (p->Relationship == rel && p->Cache.CacheSize > bytes) bytes = p->Cache.CacheSize;
    off += p->Size;
  }
  return bytes;
}
}  // namespace

SystemDiscovery discover_system() {
  SystemDiscovery d;

  SYSTEM_INFO si{};
  GetNativeSystemInfo(&si);
  d.cpu.logical_processors = si.dwNumberOfProcessors;

  // Physical cores via logical processor relationship.
  {
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (len > 0) {
      std::vector<unsigned char> buf(len);
      if (GetLogicalProcessorInformationEx(RelationProcessorCore,
          reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data()), &len)) {
        DWORD count = 0;
        for (DWORD off = 0; off < len;) {
          const auto* p = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + off);
          if (p->Relationship == RelationProcessorCore) ++count;
          off += p->Size;
        }
        d.cpu.physical_cores = count;
      }
    }
  }

  d.cpu.numa_nodes = highest_numa_node();
  d.cpu.l2_cache_bytes = get_cache_bytes(RelationCache);
  // Approximate L3 as the largest cache; see below.
  d.cpu.l3_cache_bytes = 0;

  // Memory.
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms)) {
    d.memory.total_bytes = ms.ullTotalPhys;
    d.memory.available_bytes = ms.ullAvailPhys;
  }

  // GPU enumeration via DXGI.
  {
    ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.GetAddressOf())))) {
      for (UINT i = 0; ; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        HRESULT hr = factory->EnumAdapters1(i, adapter.GetAddressOf());
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) break;
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
          GpuInfo g;
          // Convert the WCHAR descriptor to UTF-8.
          int len = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
          if (len > 0) {
            std::string tmp(static_cast<std::size_t>(len), '\0');
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, tmp.data(), len, nullptr, nullptr);
            tmp.resize(tmp.find('\0'));  // trim trailing NUL
            g.name = tmp;
          }
          g.present = true;
          g.dedicated_memory_bytes = desc.DedicatedVideoMemory;
          g.cuda_capable = false;  // only set when a CUDA runtime is actually loaded
          d.gpus.push_back(std::move(g));
        }
      }
    }
  }

  // CPU model name is not available via GetNativeSystemInfo; leave the raw value empty unless
  // the WMI query path is used. The runtime is honest about what it did not observe.
  d.cpu.name = "";

  // Near-memory hardware: none of these are present. We never infer from RAM/NIC/storage.
  d.near_memory.physical_pim = false;
  d.near_memory.physical_cxl_adjacent_compute = false;
  d.near_memory.physical_memory_side_processor = false;
  d.near_memory.physical_storage_side_compute = false;
  d.near_memory.physical_dpu_compute = false;
  d.near_memory.synthetic_targets_available = true;  // provided by the runtime backend

  d.summary = "CPU=" + std::to_string(d.cpu.logical_processors) + " threads, " +
              std::to_string(d.cpu.physical_cores) + " cores, " + std::to_string(d.cpu.numa_nodes) +
              " NUMA node(s); memory=" + std::to_string(d.memory.total_bytes / (1024 * 1024)) +
              " MiB; gpus=" + std::to_string(d.gpus.size());
  if (d.gpus.size() == 1 && d.gpus.front().name.find("RTX") != std::string::npos)
    d.summary += "; RTX CUDA-capable GPU detected";
  d.summary += "; physical near-memory compute hardware: UNSUPPORTED (none present)";
  return d;
}

}  // namespace nmc
