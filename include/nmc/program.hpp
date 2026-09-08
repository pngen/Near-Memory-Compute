#pragma once
// Versioned program/kernel identity. The v1.0.0 runtime uses a predefined bounded operation
// set; extensibility requires explicit registration and validation.

#include <cstdint>
#include <string>
#include <vector>

#include "nmc/dimension.hpp"
#include "nmc/enums.hpp"
#include "nmc/ids.hpp"

namespace nmc {

struct Program {
  KernelOrProgramId id;
  ProgramGeneration generation;
  std::string name;
  OperationClass op_class = OperationClass::SUM;
  TargetKind target_kind = TargetKind::UNKNOWN;
  std::string digest;                 // content digest (e.g. sha256 hex)
  std::vector<DataType> data_types;
  std::vector<Layout> layouts;
  std::uint64_t workspace_bytes = 0;
  ProgramLifecycle lifecycle = ProgramLifecycle::REGISTERED;
  bool predefined = true;             // true => from the bounded built-in set
  bool validated = false;
};

}  // namespace nmc
