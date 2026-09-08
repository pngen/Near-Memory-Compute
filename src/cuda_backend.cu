#include "nmc/cuda_backend.hpp"

#include <cuda_runtime.h>
#include <cstdio>

namespace nmc {

namespace {
// Kernel: atomic-add a thread-local byte sum into a 64-bit accumulator.
__global__ void reduce_sum_kernel(const unsigned char* in, unsigned long long* out, int n) {
  unsigned long long s = 0;
  for (int i = blockIdx.x * blockDim.x + threadIdx.x; i < n; i += gridDim.x * blockDim.x)
    s += in[i];
  atomicAdd(out, s);
}
}  // namespace

int cuda_device_count() {
  int n = 0;
  cudaError_t e = cudaGetDeviceCount(&n);
  return (e == cudaSuccess) ? n : 0;
}

std::vector<std::uint8_t> cuda_reduce_u8(const std::vector<std::uint8_t>& input) {
  std::vector<std::uint8_t> out;
  if (input.empty() || cuda_device_count() == 0) return out;
  const int n = static_cast<int>(input.size());
  unsigned char* d_in = nullptr;
  unsigned long long* d_out = nullptr;
  unsigned long long result = 0;
  if (cudaMalloc(&d_in, n) != cudaSuccess) return out;
  if (cudaMalloc(&d_out, sizeof(unsigned long long)) != cudaSuccess) { cudaFree(d_in); return out; }
  if (cudaMemset(d_out, 0, sizeof(unsigned long long)) != cudaSuccess) { cudaFree(d_in); cudaFree(d_out); return out; }
  if (cudaMemcpy(d_in, input.data(), n, cudaMemcpyHostToDevice) != cudaSuccess) { cudaFree(d_in); cudaFree(d_out); return out; }
  const int threads = 256;
  const int blocks = (n + threads - 1) / threads;
  reduce_sum_kernel<<<blocks, threads>>>(d_in, d_out, n);
  if (cudaDeviceSynchronize() != cudaSuccess) { cudaFree(d_in); cudaFree(d_out); return out; }
  if (cudaMemcpy(&result, d_out, sizeof(unsigned long long), cudaMemcpyDeviceToHost) != cudaSuccess) { cudaFree(d_in); cudaFree(d_out); return out; }
  cudaFree(d_in);
  cudaFree(d_out);
  out.resize(8);
  for (int i = 0; i < 8; ++i) out[i] = static_cast<std::uint8_t>((result >> (i * 8)) & 0xFF);
  return out;
}

bool cuda_conventional_proof() {
  if (cuda_device_count() == 0) return false;
  std::vector<std::uint8_t> data(1 << 16);
  for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>((i * 31 + 7) & 0xFF);
  std::uint64_t reference = 0;
  for (auto b : data) reference += b;
  auto out = cuda_reduce_u8(data);
  if (out.size() != 8) return false;
  std::uint64_t got = 0;
  for (int i = 0; i < 8; ++i) got |= static_cast<std::uint64_t>(out[i]) << (i * 8);
  return got == reference;
}

}  // namespace nmc
