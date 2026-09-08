#pragma once
// Movement-vs-compute economics. The model is deterministic and produces structured,
// human-explained terms. It never hides the decision in one opaque score.

#include <cstdint>
#include <string>
#include <vector>

#include "nmc/enums.hpp"
#include "nmc/policy.hpp"
#include "nmc/target.hpp"

namespace nmc {

struct TransferEstimate {
  std::uint64_t bytes_to_move = 0;
  std::uint64_t bandwidth_bps = 0;         // 0 => unknown; must not be treated as infinite
  std::uint64_t latency_ns = 0;
  std::uint64_t staging_cost_ns = 0;
  std::uint64_t serialization_cost_ns = 0;
  std::uint64_t registration_cost_ns = 0;
};

struct ComputeCostEstimate {
  std::uint64_t execution_ns = 0;
  std::uint64_t setup_ns = 0;
  std::uint64_t queue_delay_ns = 0;
  std::uint64_t result_return_bytes = 0;
  double confidence = 0.0;                 // [0,1]
};

struct EconomicTerms {
  std::uint64_t data_bytes_to_move = 0;
  std::uint64_t transfer_latency_ns = 0;
  std::uint64_t transfer_bytes_time_ns = 0;
  std::uint64_t staging_ns = 0;
  std::uint64_t serialization_ns = 0;
  std::uint64_t registration_ns = 0;
  std::uint64_t near_setup_ns = 0;
  std::uint64_t near_execution_ns = 0;
  std::uint64_t near_queue_delay_ns = 0;
  std::uint64_t conventional_execution_ns = 0;
  std::uint64_t result_bytes = 0;
  std::uint64_t result_transfer_ns = 0;
  std::uint64_t workspace_ns = 0;
  double confidence = 0.0;
};

struct EconomicsResult {
  Decision decision = Decision::MOVEMENT_REQUIRED;
  std::uint64_t near_total_ns = 0;
  std::uint64_t conventional_total_ns = 0;
  std::uint64_t movement_avoided_bytes = 0;
  bool near_memory_judged_beneficial = false;
  std::vector<std::string> explanation;   // human-readable, ordered
  std::vector<std::string> terms;         // "term: value unit"

  // The essential reasons in a single sentence each.
};

// Compute the conventional (move data to compute) cost. If bandwidth is unknown, the
// transfer byte-time is reported as 0 but the decision is conservatively penalized by a
// synthesis penalty so an unknown link is never assumed free.
inline EconomicTerms evaluate_terms(const TransferEstimate& tr, const ComputeCostEstimate& conv,
                                    const ComputeCostEstimate& near) {
  EconomicTerms t;
  t.data_bytes_to_move = tr.bytes_to_move;
  t.transfer_latency_ns = tr.latency_ns;
  if (tr.bandwidth_bps > 0) {
    // bytes * 1e9 / bps  -> ns
    t.transfer_bytes_time_ns =
        static_cast<std::uint64_t>((static_cast<long double>(tr.bytes_to_move) * 1'000'000'000.0L) /
                                   static_cast<long double>(tr.bandwidth_bps));
  }
  t.staging_ns = tr.staging_cost_ns;
  t.serialization_ns = tr.serialization_cost_ns;
  t.registration_ns = tr.registration_cost_ns;
  t.near_setup_ns = near.setup_ns;
  t.near_execution_ns = near.execution_ns;
  t.near_queue_delay_ns = near.queue_delay_ns;
  t.conventional_execution_ns = conv.execution_ns;
  t.result_bytes = near.result_return_bytes;
  if (tr.bandwidth_bps > 0 && near.result_return_bytes > 0) {
    t.result_transfer_ns =
        static_cast<std::uint64_t>((static_cast<long double>(near.result_return_bytes) * 1'000'000'000.0L) /
                                   static_cast<long double>(tr.bandwidth_bps));
  }
  t.workspace_ns = 0;
  t.confidence = near.confidence;
  return t;
}

// Decide between near-memory and conventional based on deterministic cost comparison.
// Returns ~0 cost differences: a lower total wins. Unknown-bandwidth transfer is penalized by
// a synthesis penalty so it cannot be treated as free.
inline EconomicsResult choose_path(const TransferEstimate& tr, const ComputeCostEstimate& conv,
                                   const ComputeCostEstimate& near) {
  EconomicTerms t = evaluate_terms(tr, conv, near);

  const std::uint64_t kSynthesisPenaltyNs = 1'000'000ull;  // 1 ms: unknown link assumed > present setup
  std::uint64_t conv_total = 0;
  conv_total += t.staging_ns + t.serialization_ns + t.registration_ns;
  conv_total += t.conventional_execution_ns;
  if (tr.bandwidth_bps > 0) {
    conv_total += t.transfer_latency_ns;
    conv_total += t.transfer_bytes_time_ns;
  } else {
    // Unknown link width is a real, non-free cost; apply a fixed synthesis penalty.
    conv_total += kSynthesisPenaltyNs;
  }

  std::uint64_t near_total = 0;
  near_total += t.near_setup_ns + t.near_execution_ns + t.near_queue_delay_ns + t.result_transfer_ns;

  EconomicsResult r;
  r.movement_avoided_bytes = t.data_bytes_to_move;
  r.near_total_ns = near_total;
  r.conventional_total_ns = conv_total;
  const bool near_beneficial = near_total <= conv_total;
  r.near_memory_judged_beneficial = near_beneficial;

  if (near_beneficial) {
    r.decision = Decision::NEAR_MEMORY_SELECTED;
  } else {
    r.decision = Decision::CONVENTIONAL_CPU_SELECTED;
  }

  r.explanation.push_back("near-memory compute: " + std::to_string(near_total) + " ns");
  r.explanation.push_back("conventional compute (move data): " + std::to_string(conv_total) + " ns");
  if (near_beneficial) {
    r.explanation.push_back("selection: NEAR_MEMORY_SELECTED because near-memory path is not more expensive than moving data.");
  } else {
    r.explanation.push_back("selection: CONVENTIONAL_COMPUTE_SELECTED because the movement path is not more expensive than near-memory setup/execution.");
  }

  r.terms.push_back("data_bytes_to_move=" + std::to_string(t.data_bytes_to_move) + " B");
  r.terms.push_back("transfer_latency=" + std::to_string(t.transfer_latency_ns) + " ns");
  r.terms.push_back("transfer_bytes_time=" + std::to_string(t.transfer_bytes_time_ns) + " ns");
  r.terms.push_back("near_setup=" + std::to_string(t.near_setup_ns) + " ns");
  r.terms.push_back("near_execution=" + std::to_string(t.near_execution_ns) + " ns");
  r.terms.push_back("near_queue_delay=" + std::to_string(t.near_queue_delay_ns) + " ns");
  r.terms.push_back("conventional_execution=" + std::to_string(t.conventional_execution_ns) + " ns");
  r.terms.push_back("result_return_bytes=" + std::to_string(t.result_bytes) + " B");
  r.terms.push_back("result_transfer=" + std::to_string(t.result_transfer_ns) + " ns");
  r.terms.push_back("confidence=" + std::to_string(t.confidence));
  return r;
}

}  // namespace nmc
