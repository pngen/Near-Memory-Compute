#pragma once
// Explicit capability model. Every capability has an explicit state; UNKNOWN required
// capabilities fail closed.

#include <map>
#include <string>
#include <vector>

#include "nmc/enums.hpp"
#include "nmc/ids.hpp"

namespace nmc {

// A single declarative capability binding a semantic key to a state.
struct CapabilityEntry {
  CapabilityId id;
  CapabilityGeneration generation;
  NearMemoryTargetId target;
  std::string key;                 // semantic key, e.g. "operation:SUM"
  CapabilityState state = CapabilityState::UNKNOWN;
  bool required_for_execution = false;
  std::string detail;
};

// A capability set for one target. The key set is closed: unknown keys are rejected on
// registration so that capability semantics cannot drift silently.
struct CapabilitySet {
  NearMemoryTargetId target;
  NearMemoryTargetGeneration target_generation;
  CapabilityGeneration generation;          // bump on any change
  std::map<std::string, CapabilityEntry> entries;
  EvidenceId evidence_id;
  EvidenceGeneration evidence_generation;
  WorkerBootId publisher;                   // worker boot that published this evidence
  bool dynamic_current = false;             // false => REVALIDATION_REQUIRED

  // Resolve a capability. Returns the state, or CapabilityState::UNKNOWN if absent.
  CapabilityState state_of(const std::string& key) const {
    auto it = entries.find(key);
    if (it == entries.end()) return CapabilityState::UNKNOWN;
    return it->second.state;
  }
};
}  // namespace nmc
