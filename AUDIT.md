# Runtime audit

This document records the results of the manual lock/deadlock audit and the hot-path scaling
audit performed during the Near-Memory Compute closure.

## Manual lock / deadlock audit

The Coordinator engine uses a single state mutex (RuntimeState guarded by one std::mutex). Every
public engine method acquires that mutex once. Internal helpers used while holding it
(staleness_locked, plan_locked, rebuild_active_plans_locked, and the encode/decoder serialization
helpers) are lock-free and are never invoked as a public method that would re-acquire the mutex,
so there is no read-to-write self-deadlock, no helper re-lock, and no lock recursion.

- No socket I/O is performed while the state lock is held. The process layer (coordinator
  accept/recv/send) is entirely outside the Coordinator engine; engine methods return state
  mutations and the process sends/receives frames without holding the state lock.
- No backend execution happens under the state lock. Result verification in apply_result only
  recomputes a content digest of the supplied output bytes; it does not dispatch or run a backend.
- No CUDA synchronization is performed under the state lock. The CUDA path is a separate backend
  exercised by the CUDA proof, not entered under the Coordinator lock.
- No thread join, process wait, or network wait occurs under the state lock. The distributed
  coordinator process waits on a std::future outside the state lock (the pending-dispatch
  promise is registered under a separate Node mutex and the wait is lock-free).
- Lock ordering: there is exactly one state lock, so no lock-order inconsistency can arise. The
  process Node mutex is a second, separate mutex held only for brief map operations, never
  together with the Coordinator state mutex in a nested order.
- Worker/session destruction races: on disconnect the process removes the worker socket and calls
  on_worker_disconnected under the Coordinator state lock via a public method; no dangling
  session ownership remains because the socket is owned by a shared_ptr released only when the
  connection thread ends.
- No result publication occurs beneath the Coordinator state lock; results are stored via a public
  method and the reply is sent by the process after the method returns.

## Hot-path scaling audit

The hot paths (plan generation, candidate filtering, ranking, reservation, completion lookup,
result provenance lookup) were audited for accidental superlinear behavior. The runtime does NOT
deep-copy the complete execution history, the full result ledger, all past plans, or monotonically
growing audit records during planning or completion.

- Plan generation: duplicate-active-plan suppression previously scanned all plans (an O(plans)
  cost per plan, i.e. O(n^2)). This was replaced with an active_plans index keyed by operation id,
  making the duplicate check O(1). No full-history copy is made during planning.
- Candidate filtering iterates only registered targets. Ranking sorts the filtered candidate list.
- Reservation and completion are constant-time map operations plus the active_plans index update.
- Result/provenance lookup is a bounded query over committed results, not a copy.
- The benchmark measures completed work (plans created, reservations, dispatched+committed
  operations) separately from planner overhead and backend execution cost at 100, 1000, 10000, and
  100000 scale. Plan generation scales approximately linearly (0, 2, 20, 237 ms across 100..100k),
  confirming no O(n^2) regression in the hot path.

## Verified warnings quality

- Release and Debug builds compile with /W4 /WX with zero first-party compiler warnings.
- The Win32 AddressSanitizer subset (engine, codec, persistence, property, concurrency,
  adversarial) runs with no sanitizer finding.
