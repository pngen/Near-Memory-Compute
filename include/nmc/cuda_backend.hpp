#pragma once
// Optional real CUDA conventional-compute backend. The GPU is NOT a near-memory compute device;
// it is a REAL conventional accelerator used to prove the economics/comparison path.

#include <cstdint>
#include <vector>

namespace nmc {

// Returns the number of CUDA-capable devices (0 if CUDA runtime unavailable).
int cuda_device_count();

// Real CUDA conventional reduction over a host buffer. Returns the 8-byte little-endian sum.
// Performs a real cudaMalloc + H2D + kernel + sync + D2H. Returns empty on error.
std::vector<std::uint8_t> cuda_reduce_u8(const std::vector<std::uint8_t>& input);

// Real CUDA operation that proves the H2D/kernel/D2H/parity/cleanup path. Returns true on success.
bool cuda_conventional_proof();

}  // namespace nmc
