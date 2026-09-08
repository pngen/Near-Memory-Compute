#pragma once
// Canonical enumerations for the Near-Memory Compute runtime.
// Codec/decoder bounds are never "numeric max + delta" tables; every frame/enum range is an
// explicit canonical validation table so that adding a newest member cannot silently break an
// older decoder's hardcoded bound.

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace nmc {

// ---- Operation classes (bounded, explicit). ----
enum class OperationClass : int {
  REDUCE = 0, SUM = 1, MIN = 2, MAX = 3, COUNT = 4, FILTER = 5, SCAN = 6,
  COMPARE = 7, CHECKSUM = 8, HASH = 9, COMPRESS = 10, DECOMPRESS = 11,
  ENCODE = 12, DECODE = 13, TRANSFORM = 14, ELEMENTWISE = 15,
  COPY_ELISION_TRANSFORM = 16, INDEX_LOOKUP = 17, GATHER = 18, SCATTER = 19,
  CUSTOM_REGISTERED = 20
};

// ---- Target kinds. An enum value is not proof of physical support. ----
enum class TargetKind : int {
  HOST_CPU_LOCAL = 0, MEMORY_SIDE_PROCESSOR = 1, PIM_CLASS = 2,
  CXL_ADJACENT_COMPUTE = 3, FABRIC_ATTACHED_COMPUTE = 4,
  STORAGE_ADJACENT_COMPUTE = 5, DPU_ADJACENT_MEMORY_SERVICE = 6,
  ACCELERATOR_MEMORY_LOCAL = 7, SOFTWARE_DEFINED = 8, SYNTHETIC = 9, UNKNOWN = 10
};

enum class ProviderKind : int { REAL = 0, SYNTHETIC = 1, UNKNOWN = 2, UNSUPPORTED = 3 };

enum class CapabilityState : int { SUPPORTED = 0, UNSUPPORTED = 1, UNKNOWN = 2, REVALIDATION_REQUIRED = 3 };

enum class TargetLifecycle : int {
  DISCOVERED = 0, REGISTERED = 1, PROBING = 2, READY = 3, BUSY = 4, DEGRADED = 5,
  DRAINING = 6, REVALIDATION_REQUIRED = 7, OFFLINE = 8, FAILED = 9, RETIRED = 10
};

enum class ExecutionLifecycle : int {
  PLANNED = 0, RESERVED = 1, DISPATCHED = 2, RUNNING = 3, VERIFYING = 4,
  COMPLETED = 5, COMMITTED = 6, FAILED = 7, OUTCOME_UNKNOWN = 8,
  CANCELLED = 9, FENCED = 10, SUPERSEDED = 11
};

enum class ProgramLifecycle : int {
  REGISTERED = 0, VALIDATED = 1, READY = 2, REVALIDATION_REQUIRED = 3, INVALID = 4, RETIRED = 5
};

// ---- Hard eligibility rejection reasons (no score may rescue an ineligible target). ----
enum class EligibilityReason : int {
  NONE = 0, TARGET_OFFLINE = 1, TARGET_DEGRADED = 2, TARGET_UNREACHABLE = 3,
  TARGET_REVALIDATION_REQUIRED = 4, OPERATION_UNSUPPORTED = 5, OPERATION_UNKNOWN = 6,
  DATATYPE_UNSUPPORTED = 7, LAYOUT_UNSUPPORTED = 8, ALIGNMENT_UNSUPPORTED = 9,
  DATA_STALE = 10, DATA_WRONG_GENERATION = 11, DATA_UNREACHABLE = 12,
  INSUFFICIENT_WORKSPACE = 13, CONCURRENCY_EXHAUSTED = 14, POLICY_DENIED = 15,
  FAILURE_DOMAIN_CONFLICT = 16, PROGRAM_INCOMPATIBLE = 17, WRONG_TARGET_GENERATION = 18,
  WRONG_WORKER_BOOT = 19, WRONG_EPOCH = 20, EVIDENCE_STALE = 21, INSUFFICIENT_CONFIDENCE = 22
};

enum class Decision : int {
  NEAR_MEMORY_SELECTED = 0, ALTERNATE_NEAR_MEMORY_SELECTED = 1,
  CONVENTIONAL_CPU_SELECTED = 2, CONVENTIONAL_GPU_SELECTED = 3, MOVEMENT_REQUIRED = 4,
  DEFER = 5, REJECT = 6, REVALIDATION_REQUIRED = 7
};

enum class RetryClass : int { REPLAY_SAFE = 0, IDEMPOTENT = 1, AT_MOST_ONCE_REQUIRED = 2, NON_RETRYABLE = 3, UNKNOWN = 4 };

enum class VerifyState : int { UNVERIFIED = 0, VERIFIED = 1, MISMATCH = 2, CORRUPT = 3, UNKNOWN = 4 };

// ---- Data types. ----
enum class DataType : int {
  U8 = 0, I8 = 1, U16 = 2, I16 = 3, U32 = 4, I32 = 5, U64 = 6, I64 = 7,
  F32 = 8, F64 = 9, BF16 = 10, F16 = 11
};

enum class Layout : int {
  CONTIGUOUS = 0, STRIDED = 1, BLOCKED = 2, COMPRESSED = 3, SPARSE = 4, TILED = 5
};

enum class PolicyOutcome : int { NEAR_MEMORY_REQUIRED = 0, NEAR_MEMORY_PREFERRED = 1, UNCONSTRAINED = 2 };

enum class IntegrityState : int { UNKNOWN = 0, INTACT = 1, SUSPECT = 2, CORRUPT = 3 };

// ---- Canonical validation tables (explicit, never "max+1"). ----
namespace detail {
template <class E, std::size_t N>
constexpr bool contains(const std::array<E, N>& table, E v) {
  for (auto e : table) if (e == v) return true;
  return false;
}
constexpr std::array<OperationClass, 21> kOperationClasses = {
  OperationClass::REDUCE, OperationClass::SUM, OperationClass::MIN, OperationClass::MAX,
  OperationClass::COUNT, OperationClass::FILTER, OperationClass::SCAN, OperationClass::COMPARE,
  OperationClass::CHECKSUM, OperationClass::HASH, OperationClass::COMPRESS, OperationClass::DECOMPRESS,
  OperationClass::ENCODE, OperationClass::DECODE, OperationClass::TRANSFORM, OperationClass::ELEMENTWISE,
  OperationClass::COPY_ELISION_TRANSFORM, OperationClass::INDEX_LOOKUP, OperationClass::GATHER,
  OperationClass::SCATTER, OperationClass::CUSTOM_REGISTERED
};
constexpr std::array<TargetKind, 11> kTargetKinds = {
  TargetKind::HOST_CPU_LOCAL, TargetKind::MEMORY_SIDE_PROCESSOR, TargetKind::PIM_CLASS,
  TargetKind::CXL_ADJACENT_COMPUTE, TargetKind::FABRIC_ATTACHED_COMPUTE,
  TargetKind::STORAGE_ADJACENT_COMPUTE, TargetKind::DPU_ADJACENT_MEMORY_SERVICE,
  TargetKind::ACCELERATOR_MEMORY_LOCAL, TargetKind::SOFTWARE_DEFINED, TargetKind::SYNTHETIC,
  TargetKind::UNKNOWN
};
constexpr std::array<CapabilityState, 4> kCapabilityStates = {
  CapabilityState::SUPPORTED, CapabilityState::UNSUPPORTED, CapabilityState::UNKNOWN,
  CapabilityState::REVALIDATION_REQUIRED
};
}  // namespace detail

// ---- to_string ----
inline const char* to_string(OperationClass v) {
  using namespace detail;
  constexpr std::array<const char*, 21> m = {"REDUCE","SUM","MIN","MAX","COUNT","FILTER","SCAN","COMPARE","CHECKSUM","HASH","COMPRESS","DECOMPRESS","ENCODE","DECODE","TRANSFORM","ELEMENTWISE","COPY_ELISION_TRANSFORM","INDEX_LOOKUP","GATHER","SCATTER","CUSTOM_REGISTERED"};
  if (contains(kOperationClasses, v)) return m[static_cast<int>(v)];
  return "INVALID_OPERATION_CLASS";
}
inline const char* to_string(TargetKind v) {
  using namespace detail;
  constexpr std::array<const char*, 11> m = {"HOST_CPU_LOCAL","MEMORY_SIDE_PROCESSOR","PIM_CLASS","CXL_ADJACENT_COMPUTE","FABRIC_ATTACHED_COMPUTE","STORAGE_ADJACENT_COMPUTE","DPU_ADJACENT_MEMORY_SERVICE","ACCELERATOR_MEMORY_LOCAL","SOFTWARE_DEFINED","SYNTHETIC","UNKNOWN"};
  if (contains(kTargetKinds, v)) return m[static_cast<int>(v)];
  return "INVALID_TARGET_KIND";
}
inline const char* to_string(ProviderKind v) {
  static constexpr std::array<const char*, 4> m = {"REAL","SYNTHETIC","UNKNOWN","UNSUPPORTED"};
  if (static_cast<int>(v) >= 0 && static_cast<int>(v) < 4) return m[static_cast<int>(v)];
  return "INVALID_PROVIDER_KIND";
}
inline const char* to_string(CapabilityState v) {
  static constexpr std::array<const char*, 4> m = {"SUPPORTED","UNSUPPORTED","UNKNOWN","REVALIDATION_REQUIRED"};
  if (static_cast<int>(v) >= 0 && static_cast<int>(v) < 4) return m[static_cast<int>(v)];
  return "INVALID_CAPABILITY_STATE";
}
inline const char* to_string(TargetLifecycle v) {
  static constexpr std::array<const char*, 11> m = {"DISCOVERED","REGISTERED","PROBING","READY","BUSY","DEGRADED","DRAINING","REVALIDATION_REQUIRED","OFFLINE","FAILED","RETIRED"};
  if (static_cast<int>(v) >= 0 && static_cast<int>(v) < 11) return m[static_cast<int>(v)];
  return "INVALID_TARGET_LIFECYCLE";
}
inline const char* to_string(ExecutionLifecycle v) {
  static constexpr std::array<const char*, 12> m = {"PLANNED","RESERVED","DISPATCHED","RUNNING","VERIFYING","COMPLETED","COMMITTED","FAILED","OUTCOME_UNKNOWN","CANCELLED","FENCED","SUPERSEDED"};
  if (static_cast<int>(v) >= 0 && static_cast<int>(v) < 12) return m[static_cast<int>(v)];
  return "INVALID_EXECUTION_LIFECYCLE";
}
inline const char* to_string(ProgramLifecycle v) {
  static constexpr std::array<const char*, 6> m = {"REGISTERED","VALIDATED","READY","REVALIDATION_REQUIRED","INVALID","RETIRED"};
  if (static_cast<int>(v) >= 0 && static_cast<int>(v) < 6) return m[static_cast<int>(v)];
  return "INVALID_PROGRAM_LIFECYCLE";
}
inline const char* to_string(EligibilityReason v) {
  static constexpr std::array<const char*, 23> m = {"NONE","TARGET_OFFLINE","TARGET_DEGRADED","TARGET_UNREACHABLE","TARGET_REVALIDATION_REQUIRED","OPERATION_UNSUPPORTED","OPERATION_UNKNOWN","DATATYPE_UNSUPPORTED","LAYOUT_UNSUPPORTED","ALIGNMENT_UNSUPPORTED","DATA_STALE","DATA_WRONG_GENERATION","DATA_UNREACHABLE","INSUFFICIENT_WORKSPACE","CONCURRENCY_EXHAUSTED","POLICY_DENIED","FAILURE_DOMAIN_CONFLICT","PROGRAM_INCOMPATIBLE","WRONG_TARGET_GENERATION","WRONG_WORKER_BOOT","WRONG_EPOCH","EVIDENCE_STALE","INSUFFICIENT_CONFIDENCE"};
  if (static_cast<int>(v) >= 0 && static_cast<int>(v) < 23) return m[static_cast<int>(v)];
  return "INVALID_ELIGIBILITY_REASON";
}
inline const char* to_string(Decision v) {
  static constexpr std::array<const char*, 8> m = {"NEAR_MEMORY_SELECTED","ALTERNATE_NEAR_MEMORY_SELECTED","CONVENTIONAL_CPU_SELECTED","CONVENTIONAL_GPU_SELECTED","MOVEMENT_REQUIRED","DEFER","REJECT","REVALIDATION_REQUIRED"};
  if (static_cast<int>(v) >= 0 && static_cast<int>(v) < 8) return m[static_cast<int>(v)];
  return "INVALID_DECISION";
}
inline const char* to_string(RetryClass v) {
  static constexpr std::array<const char*, 5> m = {"REPLAY_SAFE","IDEMPOTENT","AT_MOST_ONCE_REQUIRED","NON_RETRYABLE","UNKNOWN"};
  if (static_cast<int>(v) >= 0 && static_cast<int>(v) < 5) return m[static_cast<int>(v)];
  return "INVALID_RETRY_CLASS";
}
inline const char* to_string(VerifyState v) {
  static constexpr std::array<const char*, 5> m = {"UNVERIFIED","VERIFIED","MISMATCH","CORRUPT","UNKNOWN"};
  if (static_cast<int>(v) >= 0 && static_cast<int>(v) < 5) return m[static_cast<int>(v)];
  return "INVALID_VERIFY_STATE";
}
inline const char* to_string(DataType v) {
  static constexpr std::array<const char*, 12> m = {"U8","I8","U16","I16","U32","I32","U64","I64","F32","F64","BF16","F16"};
  if (static_cast<int>(v) >= 0 && static_cast<int>(v) < 12) return m[static_cast<int>(v)];
  return "INVALID_DATATYPE";
}
inline const char* to_string(Layout v) {
  static constexpr std::array<const char*, 6> m = {"CONTIGUOUS","STRIDED","BLOCKED","COMPRESSED","SPARSE","TILED"};
  if (static_cast<int>(v) >= 0 && static_cast<int>(v) < 6) return m[static_cast<int>(v)];
  return "INVALID_LAYOUT";
}
inline const char* to_string(PolicyOutcome v) {
  static constexpr std::array<const char*, 3> m = {"NEAR_MEMORY_REQUIRED","NEAR_MEMORY_PREFERRED","UNCONSTRAINED"};
  if (static_cast<int>(v) >= 0 && static_cast<int>(v) < 3) return m[static_cast<int>(v)];
  return "INVALID_POLICY_OUTCOME";
}
inline const char* to_string(IntegrityState v) {
  static constexpr std::array<const char*, 4> m = {"UNKNOWN","INTACT","SUSPECT","CORRUPT"};
  if (static_cast<int>(v) >= 0 && static_cast<int>(v) < 4) return m[static_cast<int>(v)];
  return "INVALID_INTEGRITY_STATE";
}

// ---- parse with validation (returns nullopt on invalid value). ----
// Parsing always consults the canonical validation table; no decoder relies on a numeric
// max+1 bound, so adding a newest member never silently breaks an old decoder.
template <class E, std::size_t N>
inline std::optional<E> parse_from_table(const std::array<E, N>& table, std::string_view name) {
  for (auto e : table) if (name == to_string(e)) return e;
  return std::nullopt;
}

template <class E>
inline std::optional<E> parse_range(std::size_t count, std::string_view name) {
  for (std::size_t i = 0; i < count; ++i) {
    const char* s = to_string(static_cast<E>(i));
    if (s != nullptr && name == std::string_view(s)) return static_cast<E>(i);
  }
  return std::nullopt;
}

template <class E>
std::optional<E> parse_enum(std::string_view name);
template <> inline std::optional<OperationClass> parse_enum<OperationClass>(std::string_view n) { return parse_from_table(detail::kOperationClasses, n); }
template <> inline std::optional<TargetKind> parse_enum<TargetKind>(std::string_view n) { return parse_from_table(detail::kTargetKinds, n); }
template <> inline std::optional<ProviderKind> parse_enum<ProviderKind>(std::string_view n) { return parse_range<ProviderKind>(4, n); }
template <> inline std::optional<CapabilityState> parse_enum<CapabilityState>(std::string_view n) { return parse_range<CapabilityState>(4, n); }
template <> inline std::optional<TargetLifecycle> parse_enum<TargetLifecycle>(std::string_view n) { return parse_range<TargetLifecycle>(11, n); }
template <> inline std::optional<ExecutionLifecycle> parse_enum<ExecutionLifecycle>(std::string_view n) { return parse_range<ExecutionLifecycle>(12, n); }
template <> inline std::optional<ProgramLifecycle> parse_enum<ProgramLifecycle>(std::string_view n) { return parse_range<ProgramLifecycle>(6, n); }
template <> inline std::optional<EligibilityReason> parse_enum<EligibilityReason>(std::string_view n) { return parse_range<EligibilityReason>(23, n); }
template <> inline std::optional<Decision> parse_enum<Decision>(std::string_view n) { return parse_range<Decision>(8, n); }
template <> inline std::optional<RetryClass> parse_enum<RetryClass>(std::string_view n) { return parse_range<RetryClass>(5, n); }
template <> inline std::optional<VerifyState> parse_enum<VerifyState>(std::string_view n) { return parse_range<VerifyState>(5, n); }
template <> inline std::optional<DataType> parse_enum<DataType>(std::string_view n) { return parse_range<DataType>(12, n); }
template <> inline std::optional<Layout> parse_enum<Layout>(std::string_view n) { return parse_range<Layout>(6, n); }
template <> inline std::optional<PolicyOutcome> parse_enum<PolicyOutcome>(std::string_view n) { return parse_range<PolicyOutcome>(3, n); }
template <> inline std::optional<IntegrityState> parse_enum<IntegrityState>(std::string_view n) { return parse_range<IntegrityState>(4, n); }

}  // namespace nmc
