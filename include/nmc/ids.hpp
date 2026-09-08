#pragma once
// Strongly typed identity and generation values for the Near-Memory Compute runtime.
// Zero is reserved as the null/sentinel value and must never be an authoritative identity.
// Generation values are monotonic (strictly-increasing) per identity kind.

#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <random>

namespace nmc {

namespace detail {
class UniqueIdSource {
 public:
  static UniqueIdSource& instance() {
    static UniqueIdSource s;
    return s;
  }
  std::uint64_t next() noexcept {
    const std::uint64_t counter = counter_.fetch_add(1, std::memory_order_relaxed);
    std::uint64_t r = random_();
    r ^= (counter * 0x9E3779B97F4A7C15ull);
    if (r == 0) r = 0x9E3779B97F4A7C15ull;
    return r;
  }
 private:
  UniqueIdSource() {
    std::random_device rd;
    std::seed_seq seq{rd(), rd(), rd(), rd()};
    prng_.seed(seq);
  }
  std::mt19937_64 prng_;
  std::atomic<uint64_t> counter_{0};
  std::mutex mu_;
  std::uint64_t random_() noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    return prng_();
  }
};
}  // namespace detail

template <class Tag>
class Id {
 public:
  using TagType = Tag;
  constexpr Id() noexcept = default;
  constexpr explicit Id(std::uint64_t value) noexcept : value_(value) {}
  [[nodiscard]] bool is_null() const noexcept { return value_ == 0; }
  [[nodiscard]] std::uint64_t value() const noexcept { return value_; }
  friend bool operator==(const Id& a, const Id& b) noexcept { return a.value_ == b.value_; }
  friend bool operator!=(const Id& a, const Id& b) noexcept { return a.value_ != b.value_; }
  friend bool operator<(const Id& a, const Id& b) noexcept { return a.value_ < b.value_; }
  static Id make() noexcept { return Id(detail::UniqueIdSource::instance().next()); }
 private:
  std::uint64_t value_ = 0;
};

template <class Tag>
class Generation {
 public:
  using TagType = Tag;
  constexpr Generation() noexcept = default;
  constexpr explicit Generation(std::uint64_t value) noexcept : value_(value) {}
  [[nodiscard]] bool is_null() const noexcept { return value_ == 0; }
  [[nodiscard]] std::uint64_t value() const noexcept { return value_; }
  [[nodiscard]] bool valid() const noexcept { return value_ != 0; }
  static constexpr Generation minimum() noexcept { return Generation(1); }
  Generation& operator++() noexcept { if (value_ != std::numeric_limits<std::uint64_t>::max()) ++value_; return *this; }
  friend bool operator==(const Generation& a, const Generation& b) noexcept { return a.value_ == b.value_; }
  friend bool operator!=(const Generation& a, const Generation& b) noexcept { return a.value_ != b.value_; }
  friend bool operator<(const Generation& a, const Generation& b) noexcept { return a.value_ < b.value_; }
  friend bool operator>(const Generation& a, const Generation& b) noexcept { return a.value_ > b.value_; }
  friend bool operator<=(const Generation& a, const Generation& b) noexcept { return a.value_ <= b.value_; }
  friend bool operator>=(const Generation& a, const Generation& b) noexcept { return a.value_ >= b.value_; }
 private:
  std::uint64_t value_ = 0;
};

struct NearMemoryTargetIdTag {};
struct NearMemoryTargetGenerationTag {};
struct MemoryDomainIdTag {};
struct MemoryDomainGenerationTag {};
struct DatasetIdTag {};
struct DatasetGenerationTag {};
struct DataRegionIdTag {};
struct DataRegionGenerationTag {};
struct OperationIdTag {};
struct OperationGenerationTag {};
struct OperationClassIdTag {};
struct KernelOrProgramIdTag {};
struct ProgramGenerationTag {};
struct CapabilityIdTag {};
struct CapabilityGenerationTag {};
struct ExecutionPlanIdTag {};
struct ExecutionPlanGenerationTag {};
struct DispatchIdTag {};
struct DispatchGenerationTag {};
struct AttemptIdTag {};
struct AttemptGenerationTag {};
struct ResultIdTag {};
struct ResultGenerationTag {};
struct ConsumerIdTag {};
struct ConsumerGenerationTag {};
struct PolicyIdTag {};
struct PolicyGenerationTag {};
struct EvidenceIdTag {};
struct EvidenceGenerationTag {};
struct WorkerIdTag {};
struct WorkerBootIdTag {};
struct CoordinatorIdTag {};
struct CoordinatorEpochTag {};
struct FailureDomainIdTag {};

using NearMemoryTargetId = Id<NearMemoryTargetIdTag>;
using NearMemoryTargetGeneration = Generation<NearMemoryTargetGenerationTag>;
using MemoryDomainId = Id<MemoryDomainIdTag>;
using MemoryDomainGeneration = Generation<MemoryDomainGenerationTag>;
using DatasetId = Id<DatasetIdTag>;
using DatasetGeneration = Generation<DatasetGenerationTag>;
using DataRegionId = Id<DataRegionIdTag>;
using DataRegionGeneration = Generation<DataRegionGenerationTag>;
using OperationId = Id<OperationIdTag>;
using OperationGeneration = Generation<OperationGenerationTag>;
using OperationClassId = Id<OperationClassIdTag>;
using KernelOrProgramId = Id<KernelOrProgramIdTag>;
using ProgramGeneration = Generation<ProgramGenerationTag>;
using CapabilityId = Id<CapabilityIdTag>;
using CapabilityGeneration = Generation<CapabilityGenerationTag>;
using ExecutionPlanId = Id<ExecutionPlanIdTag>;
using ExecutionPlanGeneration = Generation<ExecutionPlanGenerationTag>;
using DispatchId = Id<DispatchIdTag>;
using DispatchGeneration = Generation<DispatchGenerationTag>;
using AttemptId = Id<AttemptIdTag>;
using AttemptGeneration = Generation<AttemptGenerationTag>;
using ResultId = Id<ResultIdTag>;
using ResultGeneration = Generation<ResultGenerationTag>;
using ConsumerId = Id<ConsumerIdTag>;
using ConsumerGeneration = Generation<ConsumerGenerationTag>;
using PolicyId = Id<PolicyIdTag>;
using PolicyGeneration = Generation<PolicyGenerationTag>;
using EvidenceId = Id<EvidenceIdTag>;
using EvidenceGeneration = Generation<EvidenceGenerationTag>;
using WorkerId = Id<WorkerIdTag>;
using WorkerBootId = Id<WorkerBootIdTag>;
using CoordinatorId = Id<CoordinatorIdTag>;
using CoordinatorEpoch = Generation<CoordinatorEpochTag>;
using FailureDomainId = Id<FailureDomainIdTag>;

}  // namespace nmc

namespace std {
template <class Tag>
struct hash<nmc::Id<Tag>> {
  std::size_t operator()(nmc::Id<Tag> const& id) const noexcept { return std::hash<std::uint64_t>{}(id.value()); }
};
template <class Tag>
struct hash<nmc::Generation<Tag>> {
  std::size_t operator()(nmc::Generation<Tag> const& g) const noexcept { return std::hash<std::uint64_t>{}(g.value()); }
};
}  // namespace std
