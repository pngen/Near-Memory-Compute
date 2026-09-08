#pragma once
// Operator policy. Controls fallback visibility, required confidence, and economic bias.

#include <cstdint>
#include <vector>

#include "nmc/enums.hpp"
#include "nmc/ids.hpp"

namespace nmc {

struct Policy {
  PolicyId id;
  PolicyGeneration generation;
  std::string name;
  PolicyOutcome outcome = PolicyOutcome::UNCONSTRAINED;   // fallback stance
  std::vector<Decision> permitted_fallbacks;              // explicit choices allowed
  double required_confidence = 0.0;                       // [0,1] minimum confidence
  double near_memory_bias = 0.0;                          // positive => prefer near-memory
  bool reject_on_stale = true;                            // never execute stale data
  std::uint64_t max_near_memory_input_bytes = 0;          // 0 => unbounded
  bool require_verification = false;
};

}  // namespace nmc
