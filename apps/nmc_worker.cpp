#include "nmc/backend.hpp"
#include "nmc/net.hpp"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace nmc;

namespace {
std::map<std::string, std::string> parse_args(int argc, char** argv) {
  std::map<std::string, std::string> a;
  for (int i = 1; i < argc; ++i) {
    std::string s = argv[i];
    if (s.rfind("--", 0) == 0) {
      std::string key = s.substr(2);
      if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) a[key] = argv[++i];
      else a[key] = "1";
    }
  }
  return a;
}
std::uint64_t parse_u64(const std::string& s, std::uint64_t dflt) { return s.empty() ? dflt : std::stoull(s); }

void send_payload(TcpSocket& sock, FrameType type, ByteWriter& w, std::uint64_t corr) {
  std::vector<std::uint8_t> payload = w.take();
  send_frame(sock, type, std::span<const std::uint8_t>(payload.data(), payload.size()), corr);
}
bool send_msg(TcpSocket& sock, FrameType type, ByteWriter& w, std::uint64_t corr) {
  std::vector<std::uint8_t> payload = w.take();
  return send_frame(sock, type, std::span<const std::uint8_t>(payload.data(), payload.size()), corr).has_value();
}
}  // namespace

int main(int argc, char** argv) {
  auto args = parse_args(argc, argv);
  std::string coord = args.count("coordinator") ? args["coordinator"] : "127.0.0.1:0";
  std::string host = coord; std::uint16_t port = 0;
  {
    auto colon = coord.rfind(':');
    if (colon != std::string::npos) { host = coord.substr(0, colon); port = static_cast<std::uint16_t>(std::stoi(coord.substr(colon + 1))); }
  }
  std::string name = args.count("name") ? args["name"] : "worker";
  std::string target_name = args.count("target") ? args["target"] : "nm-target";
  bool synthetic = args.count("synthetic") > 0;
  bool hold_result = args.count("hold-result") > 0;
  std::uint64_t latency = parse_u64(args.count("latency") ? args["latency"] : "", 12'000'000);
  std::uint64_t throughput = parse_u64(args.count("throughput") ? args["throughput"] : "", 100ull << 30);
  std::uint64_t dataset_id = parse_u64(args.count("dataset-id") ? args["dataset-id"] : "", 1000);
  std::uint64_t region_id = parse_u64(args.count("region-id") ? args["region-id"] : "", 1001);
  std::uint64_t domain_id = parse_u64(args.count("domain-id") ? args["domain-id"] : "", 2000);
  std::uint32_t concurrency = static_cast<std::uint32_t>(parse_u64(args.count("concurrency") ? args["concurrency"] : "", 4));

  WorkerId wid = WorkerId::make();
  WorkerBootId boot = WorkerBootId::make();
  NearMemoryTargetId target = NearMemoryTargetId::make();

  std::printf("WORKER %s boot=%llu target=%llu region=%llu\n", name.c_str(),
              (unsigned long long)boot.value(), (unsigned long long)target.value(), (unsigned long long)region_id);
  std::fflush(stdout);

  auto conn = tcp_connect(host, port);
  if (!conn) { std::fprintf(stderr, "connect failed\n"); return 1; }
  TcpSocket sock = std::move(conn.value());
  std::uint64_t corr = 1;

  {
    HelloMsg m; m.worker = wid; m.worker_boot = boot; m.name = name;
    ByteWriter w; encode_hello(w, m);
    if (!send_msg(sock, FrameType::HELLO, w, corr++)) { std::fprintf(stderr, "hello send failed\n"); return 1; }
  }
  {
    RegisterTargetMsg m;
    m.target = target;
    m.generation = NearMemoryTargetGeneration(1);
    m.provider = synthetic ? ProviderKind::SYNTHETIC : ProviderKind::REAL;
    m.kind = synthetic ? TargetKind::SYNTHETIC : TargetKind::HOST_CPU_LOCAL;
    m.synthetic = synthetic;
    m.name = target_name;
    m.ops = {OperationClass::SUM, OperationClass::MIN, OperationClass::COUNT, OperationClass::MAX,
             OperationClass::FILTER, OperationClass::CHECKSUM, OperationClass::HASH};
    m.data_types = {DataType::U8};
    m.layouts = {Layout::CONTIGUOUS};
    m.max_input_bytes = 8ull << 30;
    m.alignment = 1;
    m.concurrency = concurrency;
    m.workspace = 1ull << 20;
    m.owner_boot = boot;
    ByteWriter w; encode_register_target(w, m);
    if (!send_msg(sock, FrameType::REGISTER_TARGET, w, corr++)) { std::fprintf(stderr, "register failed\n"); return 1; }
  }
  {
    CapabilityMsg m;
    m.target = target;
    m.publisher = boot;
    m.generation = CapabilityGeneration(1);
    m.evidence_generation = EvidenceGeneration(1);
    m.entries = {{"operation:SUM", CapabilityState::SUPPORTED}, {"operation:MIN", CapabilityState::SUPPORTED},
                 {"operation:MAX", CapabilityState::SUPPORTED}, {"operation:COUNT", CapabilityState::SUPPORTED},
                 {"operation:FILTER", CapabilityState::SUPPORTED}, {"operation:CHECKSUM", CapabilityState::SUPPORTED},
                 {"operation:HASH", CapabilityState::SUPPORTED}};
    ByteWriter w; encode_capability(w, m);
    if (!send_msg(sock, FrameType::CAPABILITY, w, corr++)) return 1;
  }
  {
    DataEvidenceMsg m;
    m.region = DataRegionId(region_id);
    m.region_generation = DataRegionGeneration(1);
    m.dataset = DatasetId(dataset_id);
    m.dataset_generation = DatasetGeneration(1);
    m.memory_domain = MemoryDomainId(domain_id);
    m.memory_domain_generation = MemoryDomainGeneration(1);
    m.present = true; m.current = true; m.reachable = true;
    m.publisher = boot;
    ByteWriter w; encode_data_evidence(w, m);
    if (!send_msg(sock, FrameType::DATA_EVIDENCE, w, corr++)) return 1;
  }
  {
    HeartbeatMsg m;
    m.target = target;
    m.lifecycle = TargetLifecycle::READY;
    m.reachable = true;
    m.queue_depth = 0;
    m.latency_ns = latency;
    m.throughput_bps = throughput;
    m.confidence = 0.95;
    ByteWriter w; encode_heartbeat(w, m);
    if (!send_msg(sock, FrameType::HEARTBEAT, w, corr++)) return 1;
  }

  bool running = true;
  while (running) {
    FrameReceive fr = recv_frame(sock);
    if (fr.status == FrameReceive::Status::Disconnect) break;
    if (fr.status != FrameReceive::Status::Ok) break;
    ByteReader pr(fr.payload.data(), fr.payload.size());
    switch (fr.header.type) {
      case FrameType::PING: { ByteWriter w; send_payload(sock, FrameType::PONG, w, fr.header.correlation); break; }
      case FrameType::DISPATCH: {
        auto dm = decode_dispatch(pr);
        if (!dm) break;
        BackendRequest req;
        req.op_class = dm->op_class;
        req.data_type = dm->data_type;
        req.input = std::span<const std::uint8_t>(dm->payload.data(), dm->payload.size());
        req.input_bytes = req.input.size();
        BackendResult r = synthetic ? synthetic_execute(req) : host_local_execute(req);
        if (hold_result) {
          std::printf("WORKER %s executed but withheld result (dispatch=%llu)\n", name.c_str(),
                      (unsigned long long)dm->dispatch.value());
          std::fflush(stdout);
          break;
        }
        ResultMsg rm;
        rm.plan = dm->plan; rm.plan_gen = dm->plan_gen;
        rm.dispatch = dm->dispatch; rm.attempt = dm->attempt;
        rm.target = dm->target; rm.target_gen = dm->target_gen;
        rm.worker_boot = dm->worker_boot; rm.epoch = dm->epoch;
        rm.dataset_gen = dm->dataset_gen; rm.region_gen = dm->region_gen;
        rm.operation_gen = dm->operation_gen;
        rm.result_gen = ResultGeneration(1);
        rm.output = r.output;
        rm.output_digest = to_hex(r.digest);
        rm.unknown = !r.ok;
        rm.note = r.note;
        ByteWriter w; encode_result(w, rm);
        if (!send_msg(sock, FrameType::RESULT, w, corr++)) return 1;
        break;
      }
      case FrameType::SHUTDOWN:
        return 0;
      default:
        break;
    }
  }
  return 0;
}
