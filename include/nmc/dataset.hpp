#pragma once
// Data-set and data-region evidence. The runtime binds execution to current data:
// DATA_PRESENT != DATA_CURRENT != DATA_REACHABLE.

#include <cstdint>
#include <string>

#include "nmc/dimension.hpp"
#include "nmc/enums.hpp"
#include "nmc/ids.hpp"

namespace nmc {

// A memory domain that data-bearing regions attach to.
struct MemoryDomainDescriptor {
  MemoryDomainId id;
  MemoryDomainGeneration generation;
  std::string name;
  bool durable = true;
};

// Durable dataset identity and provenance.
struct DatasetDescriptor {
  DatasetId id;
  DatasetGeneration generation;
  std::string name;
  std::string producer;         // provenance, may be empty
  bool durable = true;          // whether the dataset survives restart as a reference
};

// A storage region within a dataset, bound to a memory domain.
struct DataRegionDescriptor {
  DataRegionId id;
  DataRegionGeneration generation;
  DatasetId dataset;
  DatasetGeneration dataset_generation;
  MemoryDomainId memory_domain;
  ByteWindow window;            // offset/length within the dataset backing
  DataShape shape;
  IntegrityState integrity = IntegrityState::UNKNOWN;
  bool durable = true;
};

// Dynamic evidence: where the data currently is and whether it is current/reachable.
struct DataEvidence {
  DataRegionId region;
  DataRegionGeneration region_generation;
  DatasetGeneration dataset_generation;
  MemoryDomainId memory_domain;
  MemoryDomainGeneration memory_domain_generation;
  bool present = false;         // data is present in the domain
  bool current = false;         // generation is the latest observed
  bool reachable = false;       // reachable from an eligible target
  IntegrityState integrity = IntegrityState::UNKNOWN;
  bool mutable_ = false;
  bool dynamic_current = false; // recovered evidence must be revalidated
  EvidenceId evidence_id;
  EvidenceGeneration evidence_generation;
  WorkerBootId publisher;       // worker boot that published this evidence
  std::string location_note;
};

}  // namespace nmc
