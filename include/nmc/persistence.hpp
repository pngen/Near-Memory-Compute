#pragma once
// Versioned, integrity-checked persistence for durable state. Dynamic worker/session authority
// is NEVER persisted. On recovery, recovered dynamic evidence must become REVALIDATION_REQUIRED.

#include <filesystem>
#include <functional>
#include <optional>

#include "nmc/codec.hpp"
#include "nmc/expected.hpp"

namespace nmc {

class PersistentStore {
 public:
  // Payload envelope: [4B magic][2B version][8B payload_len][payload][4B crc32].
  static constexpr std::uint32_t kMagic = 0x504D434E;     // "NCMP"
  static constexpr std::uint16_t kFormatVersion = 1;
  static constexpr std::uint64_t kMaxPayload = 256ull * 1024 * 1024;  // 256 MiB

  using EncodeFn = std::function<void(ByteWriter&)>;
  using DecodeFn = std::function<bool(ByteReader&)>;  // must return false on malformed payload

  // Atomic save: write to a temp file, fsync, then atomically replace the target.
  // On error the original file is left untouched.
  Result<void> save(const std::filesystem::path& path, const EncodeFn& encode) const;

  // Load and verify. Returns an Error on: missing file, bad magic, bad version, bad checksum,
  // oversized payload, decode failure, or trailing garbage.
  Result<void> load(const std::filesystem::path& path, const DecodeFn& decode) const;
};

// Conservative recovery: after loading, every dynamic-capable field that was persisted must be
// marked non-current so the runtime revalidates before acting on it. This helper is applied by
// the coordinator; it makes the "restart does not restore dynamic authority" invariant explicit.
struct RecoveryRecap {
  std::size_t targets_recovered = 0;
  std::size_t dynamic_evidence_reset_to_revalidate = 0;
  std::size_t plans_fenced_or_outcome_unknown = 0;
  std::uint64_t epoch_advanced_to = 0;
};

}  // namespace nmc
