# Near-Memory Compute

**Near-Memory Compute** is a vendor-neutral runtime (C++20) that decides whether a bounded
computation should execute near the memory domain that owns or holds the relevant data, rather
than moving the data to a conventional compute target. It makes near-memory execution an
explicit, governed systems decision, not an implicit optimization.

## The systems question

For this operation and this data now, is execution near the memory domain valid and beneficial,
which near-memory target may perform it, what evidence makes that decision authoritative, and
when must the system instead move the data, fall back, defer, revalidate, or reject?

The runtime distinguishes, and never collapses:

- data is present
- data is current
- data is reachable
- operation is supported
- operation is safe on this near-memory target
- execution is economically justified
- target is ready
- execution is authorized
- result is valid
- result remains current after infrastructure state changes

## Systems boundary

Near-Memory Compute owns the control-plane decision and authority boundary for executing
supported operations close to data-bearing memory domains. It owns near-memory target identity,
memory-domain identity references, operation identity/capability/compatibility, data-set and data
generation references, target lifecycle/readiness/health/reachability, compute-capability and
memory-locality evidence, execution eligibility, hard feasibility filtering, deterministic
ranking, movement-vs-compute economics, execution-plan authority, target reservation/admission,
dispatch and completion authority, result provenance, stale-result rejection, fallback,
degradation, draining, target withdrawal, worker/process authority, durable control-plane state,
conservative restart semantics, revalidation, and inspection/explanation.

It does not own general Memory Expansion Fabric capacity management, CXL device discovery or CXL
protocol implementation, generic memory allocation, GPU memory-service allocation/residency,
Transfer Fabric movement execution, Unified Buffer ownership/lifetime, generic workload
scheduling, global accelerator placement, arbitrary compiler/runtime optimization, arbitrary
kernel compilation, full graph compilation, generic CPU scheduling, cache semantics, model
serving, RDMA registration, GPUDirect, NVLink/NVSwitch routing, NIC/DPU orchestration, general
task offload, coherent distributed shared memory, transactional database execution, PIM hardware
programming where no real backend exists, arbitrary remote procedure execution, storage-side
compute beyond the precise supported boundary, or application-level business logic.

Near-Memory Compute is not Transfer Fabric, DPU Fabric, Memory Expansion Fabric, a generic
accelerator scheduler, a compiler, a PIM simulator presented as hardware, a remote-execution
framework, or a general RPC runtime.

## Relationship to Memory Expansion Fabric

Memory Expansion Fabric answers which expanded-memory capacity exists, where it is reachable
from, which consumers may safely access it, what locality/capability/health evidence is current,
which memory region or pool is eligible, and which reservation/access authority remains valid.
Near-Memory Compute answers whether computation should occur near that data, whether the
memory-side target supports the operation, whether the operation is compatible with the data
representation, whether the movement avoided exceeds execution/setup cost, whether current
target authority permits dispatch, and whether fallback to conventional compute is required.

Near-Memory Compute may consume Memory Expansion Fabric provider evidence through a narrow,
optional, isolated adapter. The standalone core does not require Memory Expansion Fabric or any
other Summon Software Labs repository. An adapter, if used, would bind imported evidence to a
provider/source generation and never duplicate Memory Expansion Fabric state ownership.

## Relationship to Transfer Fabric

Transfer Fabric moves data. Near-Memory Compute decides whether movement should be avoided by
moving computation toward data. If it chooses conventional execution requiring movement, it may
produce an explicit movement requirement or handoff object. It never claims bytes moved unless an
actual backend performed the transfer.

## Target model

A near-memory target is an execution engine physically or logically associated with a memory
domain. Target kinds include HOST_CPU_LOCAL, MEMORY_SIDE_PROCESSOR, PIM_CLASS,
CXL_ADJACENT_COMPUTE, FABRIC_ATTACHED_COMPUTE, STORAGE_ADJACENT_COMPUTE,
DPU_ADJACENT_MEMORY_SERVICE, ACCELERATOR_MEMORY_LOCAL, SOFTWARE_DEFINED, SYNTHETIC, and UNKNOWN.
An enum value is not proof of physical support. Targets expose static/durable definition
(identity, generation, provider kind, supported operations, data types, layouts, alignment,
capacity, workspace) and separate dynamic state (lifecycle, readiness, health, reachability,
evidence generation/freshness, performance estimates, concurrency in use).

## Operation model

Near-Memory Compute governs bounded, explicit operation classes (REDUCE, SUM, MIN, MAX, COUNT,
FILTER, SCAN, COMPARE, CHECKSUM, HASH, COMPRESS, DECOMPRESS, ENCODE, DECODE, TRANSFORM,
ELEMENTWISE, COPY_ELISION_TRANSFORM, INDEX_LOOKUP, GATHER, SCATTER, CUSTOM_REGISTERED). Each
operation specifies input dataset/region, input and output sizes, data type, layout, precision,
determinism requirement, workspace, latency/throughput bounds, result-integrity requirement,
permitted fallback, policy, and compatibility requirements. The interface is never "execute
arbitrary code near memory."

## Data-generation semantics

Execution is bound to current data. Data is described by a dataset identity and generation, a data
region identity and generation, size, layout, data type, memory domain, current location,
integrity state, freshness, ownership, mutability, access rights, compatibility, and provenance. A
plan generated against DatasetGeneration N must not execute against N+1 unless the operation is
revalidated and policy permits it. Stale data generation is rejected before dispatch.

## Capability model

Capabilities are explicit. Each capability has a state (SUPPORTED, UNSUPPORTED, UNKNOWN,
REVALIDATION_REQUIRED). A required capability that is UNKNOWN fails closed.

## Lifecycle

Target lifecycle is explicit: DISCOVERED, REGISTERED, PROBING, READY, BUSY, DEGRADED, DRAINING,
REVALIDATION_REQUIRED, OFFLINE, FAILED, RETIRED. Program lifecycle is explicit: REGISTERED,
VALIDATED, READY, REVALIDATION_REQUIRED, INVALID, RETIRED. Execution lifecycle is explicit:
PLANNED, RESERVED, DISPATCHED, RUNNING, VERIFYING, COMPLETED, COMMITTED, FAILED,
OUTCOME_UNKNOWN, CANCELLED, FENCED, SUPERSEDED. Transitions are guarded; arbitrary state
assignment is rejected, and illegal transitions are tested.

## Movement-vs-compute economics

The runtime evaluates whether moving computation to data is preferable to moving data to compute
with an explicit, deterministic economics model. Terms include data bytes to move, expected
transfer latency/bandwidth, staging/serialization/registration cost, near-memory setup/execution
cost, near-memory queue delay, conventional compute execution cost, result bytes/transfer cost,
workspace cost, energy/cost when real evidence exists, recovery cost, failure risk, freshness
penalty, confidence, and policy bias. Units are time (ns) and bytes. The decision is explainable
and never hidden in one opaque score.

## Hard eligibility and deterministic ranking

Hard feasibility constraints are applied before economics. An ineligible target (offline,
degraded, unreachable, revalidation-required, unsupported/unknown operation, unsupported data
type/layout/alignment, stale/wrong-generation/unreachable data, insufficient workspace,
concurrency exhausted, policy denied, failure-domain conflict, incompatible program, wrong target
generation/worker boot/epoch, stale evidence, insufficient confidence) is never rescued by a
score. After hard filtering, valid choices are ranked deterministically (cost, movement avoided,
latency, confidence, identity as a stable tie-break). The result includes eligible candidates,
rejected candidates and reasons, expected costs, ranking factors, the selected candidate, tie-break
rule, and source evidence generations.

## Plan authority

An ExecutionPlan binds an authority snapshot (coordinator epoch, worker boot, target generation,
memory-domain/dataset/region/operation/program/capability/policy/evidence/plan generations).
Pre-dispatch revalidation is mandatory; a plan fails before dispatch if any hard authority
component changed.

## Dispatch and completion authority

Dispatch receives a unique DispatchId and AttemptId. Completion identifies the plan, dispatch,
attempt, target and its generation, worker boot, coordinator epoch, dataset/region/operation
generations, and result generation. Stale traffic is rejected before mutation. The runtime rejects
old dispatches, duplicate completions where not idempotent, conflicting completions, old target
generations, old boots, old epochs, stale datasets, superseded operations, completion after
cancellation where forbidden, completion after target retirement, and delayed results from a
fenced session.

## OUTCOME_UNKNOWN

Ambiguous execution is modeled honestly. If the target may have executed but the completion/result
acknowledgment was lost, OUTCOME_UNKNOWN is representable and the result is not committed as
success or failure. Retry classification (REPLAY_SAFE, IDEMPOTENT, AT_MOST_ONCE_REQUIRED,
NON_RETRYABLE, UNKNOWN) is explicit; a safe retry policy may depend on operation class, and the
runtime never automatically replays non-retryable work.

## Fallback

Fallback is explicit and visible. Outcomes include NEAR_MEMORY_SELECTED,
ALTERNATE_NEAR_MEMORY_SELECTED, CONVENTIONAL_CPU_SELECTED, CONVENTIONAL_GPU_SELECTED,
MOVEMENT_REQUIRED, DEFER, REJECT, REVALIDATION_REQUIRED. If policy requires near-memory
(NEAR_MEMORY_REQUIRED) and no valid target exists, the runtime rejects or defers; it never
silently falls back. If near-memory is preferred, fallback may be allowed but is visible.

## Drain and failure

begin_drain blocks new admission, preserves admitted work according to policy, exposes active
work, and transitions to a drained state only after accounting closure. Abrupt target failure
invalidates target authority, fences active plans, marks ambiguous executions appropriately,
never invents completion, and requires revalidation or fallback. The runtime does not claim
transparent migration of running work unless actually implemented.

## Persistence

Versioned, integrity-checked persistence stores durable state (target definitions, operation
definitions, program identities/digests, policy, durable dataset/region references, and bounded
plan/result history). Dynamic worker/session authority is never persisted. The format has magic,
format version, bounded counts and strings/blobs, checked arithmetic, an integrity checksum,
atomic replacement, and rejects truncation, corruption, malformed fields, invalid enums, unknown
version, and trailing garbage. Recovery advances the coordinator epoch, marks recovered dynamic
evidence REVALIDATION_REQUIRED, and fences or marks in-flight plans FENCED/SUPERSEDED/
OUTCOME_UNKNOWN. save-close-restart-load-conservative-recovery-fresh-evidence-revalidation-
fresh-execution is proven.

## Process model

A real process topology is provided: a Coordinator process and Worker processes over framed TCP
with each frame carrying magic, version, frame type (from a canonical validation table), payload
length, correlation, and a CRC-32 checksum. Real worker death (via TerminateProcess on a retained
PROCESS_INFORMATION), worker reincarnation with a fresh WorkerBootId, coordinator restart with
epoch advancement, and ack-loss/OUTCOME_UNKNOWN are all proven in the distributed test.

## REAL / SYNTHETIC / UNSUPPORTED matrix

- REAL: host-local execution (real CPU over real host memory); CUDA conventional-compute path
  (real H2D, kernel, sync, D2H, parity, cleanup) on the NVIDIA RTX 5090.
- SYNTHETIC: the near-memory target semantics and cost/ranking scenarios. These are explicitly
  labeled SYNTHETIC and are not hardware.
- UNSUPPORTED: physical PIM, physical CXL-adjacent compute, physical memory-side processor,
  physical storage-side compute, and physical DPU compute. None are present on the validated host,
  and they are reported as UNSUPPORTED, never inferred from RAM/NIC/storage presence.

The system-discovery result for the validated host reports CPU 16 logical / 8 physical cores, 1
NUMA node, about 63 GiB memory, an NVIDIA RTX 5090 CUDA-capable GPU, and physical near-memory
compute hardware: UNSUPPORTED (none present).

## Build

Requires CMake 3.24+, MSVC (Visual Studio toolset 14.44+), and a Windows SDK. Configure and build:

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DNMC_BUILD_CUDA=OFF
    cmake --build build --config Release

Options: NMC_BUILD_TESTS, NMC_BUILD_EXAMPLES, NMC_BUILD_BENCHMARKS, NMC_BUILD_CUDA, NMC_BUILD_CLI,
NMC_ASAN. The CUDA backend requires the CUDA toolkit and is built with the Ninja generator under a
Visual Studio developer environment because the VS generator does not ship a CUDA toolset here:

    call vcvars64.bat
    cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DNMC_BUILD_CUDA=ON
    cmake --build build-ninja --target nmc_test_cuda

## Test

Run the full suite with CTest or run each test executable directly:

    ctest --test-dir build -C Release --output-on-failure

The suite covers identity, lifecycle, target capability, dataset evidence, operation
compatibility, eligibility, economics, ranking, plan authority, reservation, dispatch, completion,
result verification, stale result rejection, fallback, drain, target failure, worker death,
coordinator restart, persistence, protocol, property/invariant, concurrency (real threads),
adversarial hardening, process cleanup, system discovery, synthetic near-memory scenarios, real
host-local execution, the real CUDA conventional path, and the installed consumer. Tests run
naturally with no timeouts.

## Examples

Run the example programs under build/examples/Release. Examples include basic_target,
operation_selection, movement_vs_compute, stale_data_rejection, fallback, drain, target_failure,
synthetic_pim, host_local_execution, cuda_conventional_comparison, and coordinator_recovery.

## CLI

nmc-cli provides a narrow inspection CLI: discover, targets, operations, datasets, evaluate, plan,
dispatch, results, drain, audit, inspect-state. Output exposes REAL, SYNTHETIC, UNSUPPORTED,
UNKNOWN, and REVALIDATION_REQUIRED where applicable.

## Benchmark

nmc_benchmark measures completed work (plans created, reservations, dispatched+committed
operations) separately from planner overhead and backend execution cost at 100, 1000, 10000, and
100000 scale. Plan generation scales linearly; there is no hot-path O(n^2) ledger copy.

## CMake package

Install and use via find_package:

    cmake --install build --config Release --prefix <prefix>

A downstream project can then do find_package(NearMemoryCompute CONFIG REQUIRED) and link
NearMemoryCompute::nmc. The install includes headers, the static library, the exported target, the
package configuration, and a package version file. The proof is verified by an independent
consumer built outside the source tree.

## Real hardware validation

The validated host exposes an NVIDIA RTX 5090. The CUDA test performs a real conventional
comparison path: device discovery, CUDA allocation, H2D, kernel launch, synchronization, D2H,
CPU-parity comparison, and cleanup. The GPU is never called a near-memory compute device merely
because it executes adjacent to HBM. Near-memory semantics remain SYNTHETIC unless real physical
near-memory/PIM hardware exists.

## Genuine limitations

- No physical near-memory/PIM, CXL-adjacent compute, memory-side processor, storage-side compute,
  or DPU near-memory compute hardware is present on the validated host; the runtime reports these
  as UNSUPPORTED and keeps the corresponding paths synthetic.
- The runtime does not transparently migrate running work; drain/failure handling is explicit and
  conservative.
- The CUDA backend is built with the Ninja generator (the VS generator here has no CUDA toolset).
- The synthetic backends operate on a bounded representative buffer for the operation payload; the
  logical input size drives economics. This is an explicit SYNTHETIC simplification.
- Dynamic evidence recovered from persistence becomes REVALIDATION_REQUIRED and is never treated
  as authoritative across a restart.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
