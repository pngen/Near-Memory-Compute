#pragma once
// Framed TCP protocol. Frame types use an EXPLICIT canonical validation table (never a
// numeric max+1 bound), so adding a newest frame type never silently breaks an old decoder.
// Payloads are bounded and every frame carries a CRC-32 integrity checksum.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "nmc/checksum.hpp"
#include "nmc/codec.hpp"
#include "nmc/enums.hpp"
#include "nmc/ids.hpp"
#include "nmc/operation.hpp"

namespace nmc {

constexpr std::uint32_t kProtocolMagic = 0x4D4F434E;  // "NCOM" little-endian
constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::uint32_t kMaxFramePayload = 64ull * 1024 * 1024;  // 64 MiB

enum class FrameType : std::uint16_t {
  HELLO = 0,
  REGISTER_TARGET = 1,
  CAPABILITY = 2,
  DATA_EVIDENCE = 3,
  HEARTBEAT = 4,
  DISPATCH = 5,
  DISPATCH_ACK = 6,
  RESULT = 7,
  COMMIT_ACK = 8,
  ERROR = 9,
  PING = 10,
  PONG = 11,
  SHUTDOWN = 12,
  REVALIDATE = 13,
  CLIENT_REGISTER_OPERATION = 14,
  CLIENT_PLAN = 15,
  CLIENT_DISPATCH = 16,
  CLIENT_GET_RESULT = 17,
  CLIENT_REPLY = 18
};

// Explicit canonical frame-type table (the decoder's source of truth).
constexpr std::array<FrameType, 19> kFrameTypes = {
    FrameType::HELLO, FrameType::REGISTER_TARGET, FrameType::CAPABILITY,
    FrameType::DATA_EVIDENCE, FrameType::HEARTBEAT, FrameType::DISPATCH,
    FrameType::DISPATCH_ACK, FrameType::RESULT, FrameType::COMMIT_ACK,
    FrameType::ERROR, FrameType::PING, FrameType::PONG, FrameType::SHUTDOWN,
    FrameType::REVALIDATE, FrameType::CLIENT_REGISTER_OPERATION, FrameType::CLIENT_PLAN,
    FrameType::CLIENT_DISPATCH, FrameType::CLIENT_GET_RESULT, FrameType::CLIENT_REPLY};

inline const char* to_string(FrameType v) {
  static constexpr std::array<const char*, 19> m = {"HELLO","REGISTER_TARGET","CAPABILITY","DATA_EVIDENCE","HEARTBEAT","DISPATCH","DISPATCH_ACK","RESULT","COMMIT_ACK","ERROR","PING","PONG","SHUTDOWN","REVALIDATE","CLIENT_REGISTER_OPERATION","CLIENT_PLAN","CLIENT_DISPATCH","CLIENT_GET_RESULT","CLIENT_REPLY"};
  for (std::size_t i = 0; i < kFrameTypes.size(); ++i)
    if (kFrameTypes[i] == v) return m[i];
  return "UNKNOWN_FRAME_TYPE";
}

inline bool is_valid_frame_type(FrameType v) {
  for (auto f : kFrameTypes) if (f == v) return true;
  return false;
}

// ---- Frame header. ----
struct FrameHeader {
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  FrameType type = FrameType::PING;
  std::uint32_t payload_len = 0;
  std::uint64_t correlation = 0;
  std::uint32_t checksum = 0;
};

// Header is fixed: 4 + 2 + 2 + 4 + 8 + 4 = 24 bytes.
constexpr std::size_t kFrameHeaderBytes = 24;

// Encode the fixed 24-byte header into out.
inline void encode_header(ByteWriter& out, const FrameHeader& h) noexcept {
  out.write_u32(h.magic);
  out.write_u16(h.version);
  out.write_u16(static_cast<std::uint16_t>(h.type));
  out.write_u32(h.payload_len);
  out.write_u64(h.correlation);
  out.write_u32(h.checksum);
}

// Decode the fixed 24-byte header. Returns nullopt on malformed header.
inline std::optional<FrameHeader> decode_header(ByteReader& r) noexcept {
  FrameHeader h;
  if (!r.read_u32(h.magic)) return std::nullopt;
  if (!r.read_u16(h.version)) return std::nullopt;
  std::uint16_t type_raw = 0;
  if (!r.read_u16(type_raw)) return std::nullopt;
  h.type = static_cast<FrameType>(type_raw);
  if (!is_valid_frame_type(h.type)) return std::nullopt;
  if (!r.read_u32(h.payload_len)) return std::nullopt;
  if (!r.read_u64(h.correlation)) return std::nullopt;
  if (!r.read_u32(h.checksum)) return std::nullopt;
  return h;
}

// ---- Message payloads. ----

struct HelloMsg {
  WorkerId worker;
  WorkerBootId worker_boot;
  std::string name;
};
struct RegisterTargetMsg {
  NearMemoryTargetId target;
  NearMemoryTargetGeneration generation;
  ProviderKind provider = ProviderKind::UNKNOWN;
  TargetKind kind = TargetKind::UNKNOWN;
  bool synthetic = false;
  std::string name;
  std::vector<OperationClass> ops;
  std::vector<DataType> data_types;
  std::vector<Layout> layouts;
  std::uint64_t max_input_bytes = 0;
  std::uint64_t alignment = 1;
  std::uint32_t concurrency = 1;
  std::uint64_t workspace = 0;
  WorkerBootId owner_boot;
};
struct CapabilityEntryWire { std::string key; CapabilityState state; };
struct CapabilityMsg {
  NearMemoryTargetId target;
  CapabilityGeneration generation;
  std::vector<CapabilityEntryWire> entries;
  EvidenceGeneration evidence_generation;
  WorkerBootId publisher;   // worker boot publishing this evidence
};
struct DataEvidenceMsg {
  DataRegionId region;
  DataRegionGeneration region_generation;
  DatasetId dataset;
  DatasetGeneration dataset_generation;
  MemoryDomainId memory_domain;
  MemoryDomainGeneration memory_domain_generation;
  bool present = false;
  bool current = false;
  bool reachable = false;
  WorkerBootId publisher;   // worker boot publishing this evidence
};
struct HeartbeatMsg {
  NearMemoryTargetId target;
  TargetLifecycle lifecycle = TargetLifecycle::READY;
  bool reachable = true;
  std::uint64_t queue_depth = 0;
  std::uint64_t latency_ns = 0;
  std::uint64_t throughput_bps = 0;
  double confidence = 0.0;
};
struct DispatchMsg {
  ExecutionPlanId plan;
  ExecutionPlanGeneration plan_gen;
  DispatchId dispatch;
  AttemptId attempt;
  NearMemoryTargetId target;
  NearMemoryTargetGeneration target_gen;
  WorkerBootId worker_boot;
  CoordinatorEpoch epoch;
  DatasetGeneration dataset_gen;
  DataRegionGeneration region_gen;
  OperationGeneration operation_gen;
  bool has_program = false;
  ProgramGeneration program_gen;
  OperationClass op_class = OperationClass::SUM;
  DataType data_type = DataType::U8;
  RetryClass retry_class = RetryClass::NON_RETRYABLE;
  bool idempotent = false;
  bool require_verification = false;
  std::vector<std::uint8_t> payload;
};
struct DispatchAckMsg {
  DispatchId dispatch;
  bool accepted = false;
  std::string note;
};
struct ResultMsg {
  ExecutionPlanId plan;
  ExecutionPlanGeneration plan_gen;
  DispatchId dispatch;
  AttemptId attempt;
  NearMemoryTargetId target;
  NearMemoryTargetGeneration target_gen;
  WorkerBootId worker_boot;
  CoordinatorEpoch epoch;
  DatasetGeneration dataset_gen;
  DataRegionGeneration region_gen;
  OperationGeneration operation_gen;
  ResultGeneration result_gen;
  std::vector<std::uint8_t> output;
  std::string output_digest;
  bool unknown = false;
  std::string note;
};
struct CommitAckMsg {
  ExecutionPlanId plan;
  DispatchId dispatch;
  bool committed = false;
  std::string note;
};
struct ErrorMsg { std::string code; std::string message; };
struct PingMsg {};
struct PongMsg {};
struct ShutdownMsg { std::string reason; };
struct RevalidateMsg { NearMemoryTargetId target; };

// ---- Payload encode/decode (payload_len is 0 for empty payloads). ----

inline void encode_hello(ByteWriter& w, const HelloMsg& m) {
  w.write_u64(m.worker.value()); w.write_u64(m.worker_boot.value()); w.write_string(m.name);
}
inline std::optional<HelloMsg> decode_hello(ByteReader& r) {
  HelloMsg m; std::uint64_t a=0,b=0;
  if (!r.read_u64(a) || !r.read_u64(b) || !r.read_string(m.name)) return std::nullopt;
  m.worker = WorkerId(a); m.worker_boot = WorkerBootId(b);
  return m;
}
inline void encode_register_target(ByteWriter& w, const RegisterTargetMsg& m) {
  w.write_u64(m.target.value()); w.write_u64(m.generation.value());
  w.write_u16(static_cast<std::uint16_t>(m.provider)); w.write_u16(static_cast<std::uint16_t>(m.kind));
  w.write_u8(m.synthetic ? 1 : 0); w.write_string(m.name);
  w.write_u64(m.ops.size());
  for (auto o : m.ops) w.write_u16(static_cast<std::uint16_t>(o));
  w.write_u64(m.data_types.size());
  for (auto d : m.data_types) w.write_u16(static_cast<std::uint16_t>(d));
  w.write_u64(m.layouts.size());
  for (auto l : m.layouts) w.write_u16(static_cast<std::uint16_t>(l));
  w.write_u64(m.max_input_bytes); w.write_u64(m.alignment); w.write_u32(m.concurrency);
  w.write_u64(m.workspace); w.write_u64(m.owner_boot.value());
}
inline std::optional<RegisterTargetMsg> decode_register_target(ByteReader& r) {
  RegisterTargetMsg m; std::uint64_t a=0,b=0;
  std::uint64_t nops=0,ndts=0,nlayouts=0;
  if (!r.read_u64(a) || !r.read_u64(b)) return std::nullopt;
  m.target = NearMemoryTargetId(a); m.generation = NearMemoryTargetGeneration(b);
  std::uint16_t p=0,k=0; if (!r.read_u16(p) || !r.read_u16(k)) return std::nullopt;
  m.provider = static_cast<ProviderKind>(p); m.kind = static_cast<TargetKind>(k);
  std::uint8_t syn=0; if (!r.read_u8(syn) || !r.read_string(m.name)) return std::nullopt;
  m.synthetic = syn != 0;
  if (!r.read_u64(nops)) return std::nullopt;
  for (std::uint64_t i=0;i<nops && i<64;++i){ std::uint16_t o=0; if(!r.read_u16(o)) return std::nullopt; m.ops.push_back(static_cast<OperationClass>(o)); }
  if (!r.read_u64(ndts)) return std::nullopt;
  for (std::uint64_t i=0;i<ndts && i<64;++i){ std::uint16_t o=0; if(!r.read_u16(o)) return std::nullopt; m.data_types.push_back(static_cast<DataType>(o)); }
  if (!r.read_u64(nlayouts)) return std::nullopt;
  for (std::uint64_t i=0;i<nlayouts && i<64;++i){ std::uint16_t o=0; if(!r.read_u16(o)) return std::nullopt; m.layouts.push_back(static_cast<Layout>(o)); }
  if (!r.read_u64(m.max_input_bytes) || !r.read_u64(m.alignment) || !r.read_u32(m.concurrency)) return std::nullopt;
  std::uint64_t ws=0,ob=0; if(!r.read_u64(ws)||!r.read_u64(ob)) return std::nullopt;
  m.workspace = ws; m.owner_boot = WorkerBootId(ob);
  return m;
}
inline void encode_capability(ByteWriter& w, const CapabilityMsg& m) {
  w.write_u64(m.target.value()); w.write_u64(m.generation.value());
  w.write_u64(m.entries.size());
  for (auto& e : m.entries) { w.write_string(e.key); w.write_u16(static_cast<std::uint16_t>(e.state)); }
  w.write_u64(m.evidence_generation.value());
  w.write_u64(m.publisher.value());
}
inline std::optional<CapabilityMsg> decode_capability(ByteReader& r) {
  CapabilityMsg m; std::uint64_t a=0,b=0;
  if (!r.read_u64(a) || !r.read_u64(b)) return std::nullopt;
  m.target = NearMemoryTargetId(a); m.generation = CapabilityGeneration(b);
  std::uint64_t n=0; if(!r.read_u64(n)) return std::nullopt;
  for (std::uint64_t i=0;i<n && i<1024;++i){ CapabilityEntryWire e; std::uint16_t s=0; if(!r.read_string(e.key)||!r.read_u16(s)) return std::nullopt; e.state=static_cast<CapabilityState>(s); m.entries.push_back(std::move(e)); }
  std::uint64_t eg=0; if(!r.read_u64(eg)) return std::nullopt; m.evidence_generation = EvidenceGeneration(eg);
  std::uint64_t pb=0; if(!r.read_u64(pb)) return std::nullopt; m.publisher = WorkerBootId(pb);
  return m;
}
inline void encode_data_evidence(ByteWriter& w, const DataEvidenceMsg& m) {
  w.write_u64(m.region.value()); w.write_u64(m.region_generation.value());
  w.write_u64(m.dataset.value()); w.write_u64(m.dataset_generation.value());
  w.write_u64(m.memory_domain.value()); w.write_u64(m.memory_domain_generation.value());
  w.write_u8(m.present?1:0); w.write_u8(m.current?1:0); w.write_u8(m.reachable?1:0); w.write_u64(m.publisher.value());
}
inline std::optional<DataEvidenceMsg> decode_data_evidence(ByteReader& r) {
  DataEvidenceMsg m;
  std::uint64_t region=0, region_gen=0, dataset=0, dataset_gen=0, domain=0, domain_gen=0;
  std::uint8_t pr=0, cu=0, re=0;
  if (!r.read_u64(region) || !r.read_u64(region_gen) || !r.read_u64(dataset) || !r.read_u64(dataset_gen)
      || !r.read_u64(domain) || !r.read_u64(domain_gen)) return std::nullopt;
  if (!r.read_u8(pr) || !r.read_u8(cu) || !r.read_u8(re)) return std::nullopt;
  m.region = DataRegionId(region); m.region_generation = DataRegionGeneration(region_gen);
  m.dataset = DatasetId(dataset); m.dataset_generation = DatasetGeneration(dataset_gen);
  m.memory_domain = MemoryDomainId(domain); m.memory_domain_generation = MemoryDomainGeneration(domain_gen);
  m.present = pr != 0; m.current = cu != 0; m.reachable = re != 0;
  std::uint64_t pb=0; if(!r.read_u64(pb)) return std::nullopt; m.publisher = WorkerBootId(pb);
  return m;
}
inline void encode_heartbeat(ByteWriter& w, const HeartbeatMsg& m) {
  w.write_u64(m.target.value()); w.write_u16(static_cast<std::uint16_t>(m.lifecycle));
  w.write_u8(m.reachable?1:0); w.write_u64(m.queue_depth);
  w.write_u64(m.latency_ns); w.write_u64(m.throughput_bps); w.write_f64(m.confidence);
}
inline std::optional<HeartbeatMsg> decode_heartbeat(ByteReader& r) {
  HeartbeatMsg m; std::uint64_t t=0; std::uint16_t lc=0; std::uint8_t re=0;
  if (!r.read_u64(t) || !r.read_u16(lc) || !r.read_u8(re)) return std::nullopt;
  m.target = NearMemoryTargetId(t); m.lifecycle = static_cast<TargetLifecycle>(lc); m.reachable = re != 0;
  if (!r.read_u64(m.queue_depth) || !r.read_u64(m.latency_ns) || !r.read_u64(m.throughput_bps) || !r.read_f64(m.confidence)) return std::nullopt;
  return m;
}
inline void encode_dispatch(ByteWriter& w, const DispatchMsg& m) {
  w.write_u64(m.plan.value()); w.write_u64(m.plan_gen.value());
  w.write_u64(m.dispatch.value()); w.write_u64(m.attempt.value());
  w.write_u64(m.target.value()); w.write_u64(m.target_gen.value());
  w.write_u64(m.worker_boot.value()); w.write_u64(m.epoch.value());
  w.write_u64(m.dataset_gen.value()); w.write_u64(m.region_gen.value());
  w.write_u64(m.operation_gen.value());
  w.write_u8(m.has_program?1:0); if (m.has_program) w.write_u64(m.program_gen.value());
  w.write_u16(static_cast<std::uint16_t>(m.op_class)); w.write_u16(static_cast<std::uint16_t>(m.data_type));
  w.write_u16(static_cast<std::uint16_t>(m.retry_class));
  w.write_u8(m.idempotent?1:0); w.write_u8(m.require_verification?1:0);
  w.write_blob(m.payload.data(), m.payload.size());
}
inline std::optional<DispatchMsg> decode_dispatch(ByteReader& r) {
  DispatchMsg m; std::uint64_t v=0;
  if (!r.read_u64(v)) return std::nullopt; m.plan = ExecutionPlanId(v);
  if (!r.read_u64(v)) return std::nullopt; m.plan_gen = ExecutionPlanGeneration(v);
  if (!r.read_u64(v)) return std::nullopt; m.dispatch = DispatchId(v);
  if (!r.read_u64(v)) return std::nullopt; m.attempt = AttemptId(v);
  if (!r.read_u64(v)) return std::nullopt; m.target = NearMemoryTargetId(v);
  if (!r.read_u64(v)) return std::nullopt; m.target_gen = NearMemoryTargetGeneration(v);
  if (!r.read_u64(v)) return std::nullopt; m.worker_boot = WorkerBootId(v);
  if (!r.read_u64(v)) return std::nullopt; m.epoch = CoordinatorEpoch(v);
  if (!r.read_u64(v)) return std::nullopt; m.dataset_gen = DatasetGeneration(v);
  if (!r.read_u64(v)) return std::nullopt; m.region_gen = DataRegionGeneration(v);
  if (!r.read_u64(v)) return std::nullopt; m.operation_gen = OperationGeneration(v);
  std::uint8_t hp=0; if(!r.read_u8(hp)) return std::nullopt; m.has_program = hp != 0;
  if (m.has_program) { if(!r.read_u64(v)) return std::nullopt; m.program_gen = ProgramGeneration(v); }
  std::uint16_t oc=0,dt=0,rc=0; std::uint8_t idem=0,rv=0;
  if(!r.read_u16(oc)||!r.read_u16(dt)||!r.read_u16(rc)||!r.read_u8(idem)||!r.read_u8(rv)) return std::nullopt;
  m.op_class = static_cast<OperationClass>(oc); m.data_type = static_cast<DataType>(dt);
  m.retry_class = static_cast<RetryClass>(rc); m.idempotent = idem != 0; m.require_verification = rv != 0;
  if (!r.read_blob(m.payload)) return std::nullopt;
  return m;
}
inline void encode_dispatch_ack(ByteWriter& w, const DispatchAckMsg& m) {
  w.write_u64(m.dispatch.value()); w.write_u8(m.accepted?1:0); w.write_string(m.note);
}
inline std::optional<DispatchAckMsg> decode_dispatch_ack(ByteReader& r) {
  DispatchAckMsg m; std::uint64_t d=0; std::uint8_t a=0;
  if(!r.read_u64(d)||!r.read_u8(a)||!r.read_string(m.note)) return std::nullopt;
  m.dispatch = DispatchId(d); m.accepted = a != 0; return m;
}
inline void encode_result(ByteWriter& w, const ResultMsg& m) {
  w.write_u64(m.plan.value()); w.write_u64(m.plan_gen.value());
  w.write_u64(m.dispatch.value()); w.write_u64(m.attempt.value());
  w.write_u64(m.target.value()); w.write_u64(m.target_gen.value());
  w.write_u64(m.worker_boot.value()); w.write_u64(m.epoch.value());
  w.write_u64(m.dataset_gen.value()); w.write_u64(m.region_gen.value());
  w.write_u64(m.operation_gen.value()); w.write_u64(m.result_gen.value());
  w.write_blob(m.output.data(), m.output.size()); w.write_string(m.output_digest);
  w.write_u8(m.unknown?1:0); w.write_string(m.note);
}
inline std::optional<ResultMsg> decode_result(ByteReader& r) {
  ResultMsg m; std::uint64_t v=0;
  if (!r.read_u64(v)) return std::nullopt; m.plan = ExecutionPlanId(v);
  if (!r.read_u64(v)) return std::nullopt; m.plan_gen = ExecutionPlanGeneration(v);
  if (!r.read_u64(v)) return std::nullopt; m.dispatch = DispatchId(v);
  if (!r.read_u64(v)) return std::nullopt; m.attempt = AttemptId(v);
  if (!r.read_u64(v)) return std::nullopt; m.target = NearMemoryTargetId(v);
  if (!r.read_u64(v)) return std::nullopt; m.target_gen = NearMemoryTargetGeneration(v);
  if (!r.read_u64(v)) return std::nullopt; m.worker_boot = WorkerBootId(v);
  if (!r.read_u64(v)) return std::nullopt; m.epoch = CoordinatorEpoch(v);
  if (!r.read_u64(v)) return std::nullopt; m.dataset_gen = DatasetGeneration(v);
  if (!r.read_u64(v)) return std::nullopt; m.region_gen = DataRegionGeneration(v);
  if (!r.read_u64(v)) return std::nullopt; m.operation_gen = OperationGeneration(v);
  if (!r.read_u64(v)) return std::nullopt; m.result_gen = ResultGeneration(v);
  if (!r.read_blob(m.output) || !r.read_string(m.output_digest)) return std::nullopt;
  std::uint8_t un=0; if(!r.read_u8(un)||!r.read_string(m.note)) return std::nullopt;
  m.unknown = un != 0; return m;
}
inline void encode_commit_ack(ByteWriter& w, const CommitAckMsg& m) {
  w.write_u64(m.plan.value()); w.write_u64(m.dispatch.value()); w.write_u8(m.committed?1:0); w.write_string(m.note);
}
inline std::optional<CommitAckMsg> decode_commit_ack(ByteReader& r) {
  CommitAckMsg m; std::uint64_t p=0,d=0; std::uint8_t c=0;
  if(!r.read_u64(p)||!r.read_u64(d)||!r.read_u8(c)||!r.read_string(m.note)) return std::nullopt;
  m.plan = ExecutionPlanId(p); m.dispatch = DispatchId(d); m.committed = c != 0; return m;
}
inline void encode_error(ByteWriter& w, const ErrorMsg& m) { w.write_string(m.code); w.write_string(m.message); }
inline std::optional<ErrorMsg> decode_error(ByteReader& r) { ErrorMsg m; if(!r.read_string(m.code)||!r.read_string(m.message)) return std::nullopt; return m; }
inline void encode_shutdown(ByteWriter& w, const ShutdownMsg& m) { w.write_string(m.reason); }
inline std::optional<ShutdownMsg> decode_shutdown(ByteReader& r) { ShutdownMsg m; if(!r.read_string(m.reason)) return std::nullopt; return m; }
inline void encode_revalidate(ByteWriter& w, const RevalidateMsg& m) { w.write_u64(m.target.value()); }
inline std::optional<RevalidateMsg> decode_revalidate(ByteReader& r) { RevalidateMsg m; std::uint64_t t=0; if(!r.read_u64(t)) return std::nullopt; m.target = NearMemoryTargetId(t); return m; }

// ---- Controller (client) messages. ----
struct ClientRegisterOperationMsg { OperationSpec op; };
struct ClientPlanMsg { OperationId op; };
struct ClientDispatchMsg { ExecutionPlanId plan; };
struct ClientGetResultMsg { ExecutionPlanId plan; };
struct ClientReplyMsg {
  bool ok = false;
  ExecutionPlanId plan;
  Decision decision = Decision::REJECT;
  bool created = false;
  bool committed = false;
  bool ambiguous = false;
  VerifyState verification = VerifyState::UNVERIFIED;
  std::uint64_t output_bytes = 0;
  std::string output_digest;
  std::string note;
};

inline void encode_operation_fields(ByteWriter& w, const OperationSpec& op) {
  w.write_u64(op.id.value()); w.write_u64(op.generation.value());
  w.write_u16(static_cast<std::uint16_t>(op.op_class));
  w.write_u64(op.dataset.value()); w.write_u64(op.dataset_generation.value());
  w.write_u64(op.region.value()); w.write_u64(op.region_generation.value());
  w.write_u64(op.shape.element_count); w.write_u16(static_cast<std::uint16_t>(op.shape.data_type));
  w.write_u16(static_cast<std::uint16_t>(op.shape.layout)); w.write_u64(op.shape.alignment);
  w.write_u64(op.input_bytes); w.write_u64(op.output_bytes); w.write_u64(op.workspace_bytes);
  w.write_u16(static_cast<std::uint16_t>(op.output_data_type));
  w.write_u8(op.determinism_required ? 1 : 0);
  w.write_u64(op.max_tolerated_latency_ns); w.write_u64(op.required_throughput_bps);
  w.write_u16(static_cast<std::uint16_t>(op.integrity)); w.write_u16(static_cast<std::uint16_t>(op.retry_class));
  w.write_u64(op.policy.value()); w.write_u64(op.policy_generation.value());
  w.write_u8(op.program.has_value() ? 1 : 0);
  if (op.program.has_value()) {
    w.write_u64(op.program.value().value());
    w.write_u64(op.program_generation.value_or(ProgramGeneration(1)).value());
  }
  w.write_string(op.description);
}

inline std::optional<OperationSpec> decode_operation_fields(ByteReader& r) {
  OperationSpec op; std::uint64_t v = 0;
  if (!r.read_u64(v)) return std::nullopt; op.id = OperationId(v);
  if (!r.read_u64(v)) return std::nullopt; op.generation = OperationGeneration(v);
  std::uint16_t oc = 0; if (!r.read_u16(oc)) return std::nullopt; op.op_class = static_cast<OperationClass>(oc);
  if (!r.read_u64(v)) return std::nullopt; op.dataset = DatasetId(v);
  if (!r.read_u64(v)) return std::nullopt; op.dataset_generation = DatasetGeneration(v);
  if (!r.read_u64(v)) return std::nullopt; op.region = DataRegionId(v);
  if (!r.read_u64(v)) return std::nullopt; op.region_generation = DataRegionGeneration(v);
  if (!r.read_u64(op.shape.element_count)) return std::nullopt;
  std::uint16_t dt = 0; if (!r.read_u16(dt)) return std::nullopt; op.shape.data_type = static_cast<DataType>(dt);
  std::uint16_t ly = 0; if (!r.read_u16(ly)) return std::nullopt; op.shape.layout = static_cast<Layout>(ly);
  if (!r.read_u64(op.shape.alignment)) return std::nullopt;
  if (!r.read_u64(op.input_bytes) || !r.read_u64(op.output_bytes) || !r.read_u64(op.workspace_bytes)) return std::nullopt;
  std::uint16_t odt = 0; if (!r.read_u16(odt)) return std::nullopt; op.output_data_type = static_cast<DataType>(odt);
  std::uint8_t det = 0; if (!r.read_u8(det)) return std::nullopt; op.determinism_required = det != 0;
  if (!r.read_u64(op.max_tolerated_latency_ns) || !r.read_u64(op.required_throughput_bps)) return std::nullopt;
  std::uint16_t ig = 0; if (!r.read_u16(ig)) return std::nullopt; op.integrity = static_cast<IntegrityRequirement>(ig);
  std::uint16_t rc = 0; if (!r.read_u16(rc)) return std::nullopt; op.retry_class = static_cast<RetryClass>(rc);
  if (!r.read_u64(v)) return std::nullopt; op.policy = PolicyId(v);
  if (!r.read_u64(v)) return std::nullopt; op.policy_generation = PolicyGeneration(v);
  std::uint8_t hp = 0; if (!r.read_u8(hp)) return std::nullopt;
  if (hp != 0) {
    if (!r.read_u64(v)) return std::nullopt; op.program = KernelOrProgramId(v);
    if (!r.read_u64(v)) return std::nullopt; op.program_generation = ProgramGeneration(v);
  }
  if (!r.read_string(op.description)) return std::nullopt;
  return op;
}

inline void encode_client_register_op(ByteWriter& w, const ClientRegisterOperationMsg& m) { encode_operation_fields(w, m.op); }
inline std::optional<ClientRegisterOperationMsg> decode_client_register_op(ByteReader& r) {
  ClientRegisterOperationMsg m; auto op = decode_operation_fields(r); if (!op) return std::nullopt; m.op = *op; return m;
}
inline void encode_client_plan(ByteWriter& w, const ClientPlanMsg& m) { w.write_u64(m.op.value()); }
inline std::optional<ClientPlanMsg> decode_client_plan(ByteReader& r) { ClientPlanMsg m; std::uint64_t v=0; if(!r.read_u64(v)) return std::nullopt; m.op = OperationId(v); return m; }
inline void encode_client_dispatch(ByteWriter& w, const ClientDispatchMsg& m) { w.write_u64(m.plan.value()); }
inline std::optional<ClientDispatchMsg> decode_client_dispatch(ByteReader& r) { ClientDispatchMsg m; std::uint64_t v=0; if(!r.read_u64(v)) return std::nullopt; m.plan = ExecutionPlanId(v); return m; }
inline void encode_client_get_result(ByteWriter& w, const ClientGetResultMsg& m) { w.write_u64(m.plan.value()); }
inline std::optional<ClientGetResultMsg> decode_client_get_result(ByteReader& r) { ClientGetResultMsg m; std::uint64_t v=0; if(!r.read_u64(v)) return std::nullopt; m.plan = ExecutionPlanId(v); return m; }

inline void encode_client_reply(ByteWriter& w, const ClientReplyMsg& m) {
  w.write_u8(m.ok ? 1 : 0); w.write_u64(m.plan.value());
  w.write_u16(static_cast<std::uint16_t>(m.decision));
  w.write_u8(m.created ? 1 : 0); w.write_u8(m.committed ? 1 : 0); w.write_u8(m.ambiguous ? 1 : 0);
  w.write_u16(static_cast<std::uint16_t>(m.verification));
  w.write_u64(m.output_bytes); w.write_string(m.output_digest); w.write_string(m.note);
}
inline std::optional<ClientReplyMsg> decode_client_reply(ByteReader& r) {
  ClientReplyMsg m; std::uint8_t ok=0; std::uint64_t v=0;
  if (!r.read_u8(ok) || !r.read_u64(v)) return std::nullopt;
  m.ok = ok != 0; m.plan = ExecutionPlanId(v);
  std::uint16_t d=0; if(!r.read_u16(d)) return std::nullopt; m.decision = static_cast<Decision>(d);
  std::uint8_t cr=0,co=0,am=0; std::uint16_t vf=0;
  if(!r.read_u8(cr)||!r.read_u8(co)||!r.read_u8(am)||!r.read_u16(vf)) return std::nullopt;
  m.created = cr != 0; m.committed = co != 0; m.ambiguous = am != 0;
  m.verification = static_cast<VerifyState>(vf);
  if(!r.read_u64(m.output_bytes)||!r.read_string(m.output_digest)||!r.read_string(m.note)) return std::nullopt;
  return m;
}

}  // namespace nmc
