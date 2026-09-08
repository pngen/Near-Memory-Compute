#include "nmc/net.hpp"
#include "proc.hpp"
#include "test_framework.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <thread>

using namespace nmc;

namespace {

const std::filesystem::path kRoot = std::filesystem::temp_directory_path() / "nmc_distributed";

// The coordinator/worker executables are at <build>/<Config>, derived from the test's own path.
std::filesystem::path g_build_root;
std::string g_config;
std::filesystem::path exe_path(const char* name) {
  return g_build_root / g_config / name;
}

Result<TcpSocket> connect_client(uint16_t port) {
  return tcp_connect("127.0.0.1", port);
}

Result<ClientReplyMsg> send_client(TcpSocket& sock, FrameType type, ByteWriter& w, uint64_t& corr) {
  auto p = w.take();
  auto sr = send_frame(sock, type, std::span<const std::uint8_t>(p.data(), p.size()), corr++);
  if (!sr) return make_error("E_SEND", "client send failed");
  FrameReceive fr = recv_frame(sock);
  if (fr.status != FrameReceive::Status::Ok) return make_error("E_RECV", "client recv failed");
  ByteReader pr(fr.payload.data(), fr.payload.size());
  auto reply = decode_client_reply(pr);
  if (!reply) return make_error("E_DECODE", "bad client reply");
  return *reply;
}

Result<ClientReplyMsg> register_op(TcpSocket& sock, OperationSpec op, uint64_t& corr) {
  ByteWriter w; encode_client_register_op(w, ClientRegisterOperationMsg{op});
  return send_client(sock, FrameType::CLIENT_REGISTER_OPERATION, w, corr);
}
Result<ClientReplyMsg> plan_op(TcpSocket& sock, OperationId op, uint64_t& corr) {
  ByteWriter w; encode_client_plan(w, ClientPlanMsg{op});
  return send_client(sock, FrameType::CLIENT_PLAN, w, corr);
}
Result<ClientReplyMsg> dispatch_plan(TcpSocket& sock, ExecutionPlanId plan, uint64_t& corr) {
  ByteWriter w; encode_client_dispatch(w, ClientDispatchMsg{plan});
  return send_client(sock, FrameType::CLIENT_DISPATCH, w, corr);
}
Result<ClientReplyMsg> get_result(TcpSocket& sock, ExecutionPlanId plan, uint64_t& corr) {
  ByteWriter w; encode_client_get_result(w, ClientGetResultMsg{plan});
  return send_client(sock, FrameType::CLIENT_GET_RESULT, w, corr);
}

OperationSpec make_op(OperationId id, uint64_t dataset, uint64_t region) {
  OperationSpec op;
  op.id = id; op.generation = OperationGeneration(1);
  op.op_class = OperationClass::SUM;
  op.dataset = DatasetId(dataset); op.dataset_generation = DatasetGeneration(1);
  op.region = DataRegionId(region); op.region_generation = DataRegionGeneration(1);
  op.shape = DataShape{8ull * 1024 * 1024 * 1024, DataType::U8, Layout::CONTIGUOUS, 1};
  op.input_bytes = 8ull * 1024 * 1024 * 1024; op.output_bytes = 8;
  op.output_data_type = DataType::U64;
  op.retry_class = RetryClass::REPLAY_SAFE;
  op.integrity = IntegrityRequirement::CHECKED;
  op.policy = PolicyId(1); op.policy_generation = PolicyGeneration(1);
  return op;
}

uint16_t start_coordinator(proc::Child& coordinator, const std::string& persist, uint64_t epoch) {
  std::vector<std::string> args = {"--port", "0"};
  if (!persist.empty()) { args.push_back("--persist"); args.push_back(persist); }
  if (epoch) { args.push_back("--epoch"); args.push_back(std::to_string(epoch)); }
  auto sp = proc::spawn(exe_path("nmc-coordinator.exe").string(), args, true);
  if (!sp) { std::printf("  spawn coordinator failed: %s\n", sp.error().message.c_str()); return 0; }
  coordinator = std::move(sp.value());
  std::string line;
  if (!coordinator.read_line(line)) { std::printf("  coordinator no port line\n"); return 0; }
  // line is "PORT n"
  std::size_t p = line.find(' ');
  if (p == std::string::npos) return 0;
  return static_cast<uint16_t>(std::stoi(line.substr(p + 1)));
}

void start_worker(proc::Child& worker, uint16_t port, const std::string& name, bool synthetic, uint64_t dataset, uint64_t region, bool hold) {
  std::vector<std::string> args = {"--coordinator", "127.0.0.1:" + std::to_string(port), "--name", name};
  if (synthetic) args.push_back("--synthetic");
  if (hold) args.push_back("--hold-result");
  args.push_back("--dataset-id"); args.push_back(std::to_string(dataset));
  args.push_back("--region-id"); args.push_back(std::to_string(region));
  auto sp = proc::spawn(exe_path("nmc-worker.exe").string(), args, true);
  if (!sp) { std::printf("  spawn worker failed\n"); return; }
  worker = std::move(sp.value());
  std::string line;
  worker.read_line(line);  // "WORKER <name> boot=..."
}

}  // namespace

NMC_TEST(distributed_worker_death_reincarnation) {
  std::filesystem::create_directories(kRoot);
  std::string persist = (kRoot / "worker_death.bin").string();
  std::filesystem::remove(persist);

  proc::Child coordinator;
  uint16_t port = start_coordinator(coordinator, persist, 0);
  CHECK(port != 0);
  proc::Child workerA;
  start_worker(workerA, port, "A", true, 1000, 1001, false);
  CHECK(workerA.started);
  std::uint32_t pidA = workerA.pid();
  CHECK(pidA != 0);

  auto client = connect_client(port);
  CHECK(client.has_value());
  TcpSocket c = std::move(client.value());
  uint64_t corr = 100;
  OperationId op1 = OperationId::make();
  auto r1 = register_op(c, make_op(op1, 1000, 1001), corr);
  CHECK(r1.has_value() && r1.value().ok);
  auto pl1 = plan_op(c, op1, corr);
  CHECK(pl1.has_value() && pl1.value().created);
  ExecutionPlanId plan1 = pl1.value().plan;
  auto dp1 = dispatch_plan(c, plan1, corr);
  CHECK(dp1.has_value());
  CHECK(dp1.value().committed);  // worker A executed and committed
  auto g1 = get_result(c, plan1, corr);
  CHECK(g1.has_value() && g1.value().committed);
  CHECK(g1.value().verification == VerifyState::VERIFIED);

  // Kill worker A as a real OS process; retain and verify the handle/PID used.
  bool killed = workerA.stop(1);
  CHECK(killed);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // The old boot's authority is fenced: a fresh plan for the same data must NOT be eligible.
  OperationId op2 = OperationId::make();
  auto r2 = register_op(c, make_op(op2, 1000, 1001), corr);
  CHECK(r2.has_value() && r2.value().ok);
  auto pl2 = plan_op(c, op2, corr);
  CHECK(pl2.has_value());
  CHECK(!pl2.value().created);  // no eligible target after worker death

  // Reincarnate: A-prime with a fresh WorkerBootId republishes current evidence.
  proc::Child workerA2;
  start_worker(workerA2, port, "A-prime", true, 1000, 1001, false);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  OperationId op3 = OperationId::make();
  auto r3 = register_op(c, make_op(op3, 1000, 1001), corr);
  CHECK(r3.has_value() && r3.value().ok);
  auto pl3 = plan_op(c, op3, corr);
  CHECK(pl3.has_value());
  CHECK(pl3.value().created);  // A-prime is eligible
  ExecutionPlanId plan3 = pl3.value().plan;
  auto dp3 = dispatch_plan(c, plan3, corr);
  CHECK(dp3.has_value());
  CHECK(dp3.value().committed);  // executes under current authority

  workerA2.stop(1);
  coordinator.stop(1);
  std::filesystem::remove(persist);
}

NMC_TEST(distributed_coordinator_restart) {
  std::filesystem::create_directories(kRoot);
  std::string persist = (kRoot / "restart.bin").string();
  std::filesystem::remove(persist);

  proc::Child coordinator;
  uint16_t port = start_coordinator(coordinator, persist, 1);
  CHECK(port != 0);
  proc::Child worker;
  start_worker(worker, port, "A", true, 2000, 2001, false);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  auto client = connect_client(port);
  CHECK(client.has_value());
  TcpSocket c = std::move(client.value());
  uint64_t corr = 200;
  OperationId op1 = OperationId::make();
  CHECK(register_op(c, make_op(op1, 2000, 2001), corr).has_value());
  auto pl1 = plan_op(c, op1, corr);
  CHECK(pl1.has_value() && pl1.value().created);
  ExecutionPlanId plan1 = pl1.value().plan;
  auto dp1 = dispatch_plan(c, plan1, corr);
  CHECK(dp1.has_value() && dp1.value().committed);  // this commit persists state

  // Kill the coordinator as a real OS process.
  std::uint32_t coord_pid = coordinator.pid();
  CHECK(coord_pid != 0);
  bool killed = coordinator.stop(1);
  CHECK(killed);
  worker.stop(1);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Restart: new coordinator process, epoch advances past the saved epoch.
  proc::Child coordinator2;
  uint16_t port2 = start_coordinator(coordinator2, persist, 0);
  CHECK(port2 != 0);
  // Fresh worker registers on the new epoch.
  proc::Child worker2;
  start_worker(worker2, port2, "B", true, 2000, 2001, false);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  auto client2 = connect_client(port2);
  CHECK(client2.has_value());
  TcpSocket c2 = std::move(client2.value());
  uint64_t corr2 = 300;

  // Durability: the previously committed result survives restart.
  auto g1 = get_result(c2, plan1, corr2);
  CHECK(g1.has_value() && g1.value().committed);
  CHECK(g1.value().verification == VerifyState::VERIFIED);

  // A fresh plan under the new epoch works after the worker re-registers.
  OperationId op2 = OperationId::make();
  CHECK(register_op(c2, make_op(op2, 2000, 2001), corr2).has_value());
  auto pl2 = plan_op(c2, op2, corr2);
  CHECK(pl2.has_value() && pl2.value().created);
  ExecutionPlanId plan2 = pl2.value().plan;
  auto dp2 = dispatch_plan(c2, plan2, corr2);
  CHECK(dp2.has_value() && dp2.value().committed);

  worker2.stop(1);
  coordinator2.stop(1);
  std::filesystem::remove(persist);
}

NMC_TEST(distributed_ack_loss_outcome_unknown) {
  std::filesystem::create_directories(kRoot);
  std::string persist = (kRoot / "ackloss.bin").string();
  std::filesystem::remove(persist);
  proc::Child coordinator;
  uint16_t port = start_coordinator(coordinator, persist, 0);
  CHECK(port != 0);
  proc::Child worker;
  start_worker(worker, port, "A", true, 3000, 3001, true /* hold result */);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  auto client = connect_client(port);
  CHECK(client.has_value());
  TcpSocket c = std::move(client.value());
  uint64_t corr = 400;
  OperationId op = OperationId::make();
  CHECK(register_op(c, make_op(op, 3000, 3001), corr).has_value());
  auto pl = plan_op(c, op, corr);
  CHECK(pl.has_value() && pl.value().created);
  ExecutionPlanId plan = pl.value().plan;

  // The worker WITHHOLDs the result; the coordinator must not claim success. Dispatch is issued
  // on a detached thread because the coordinator blocks waiting for the (withheld) result.
  std::promise<ClientReplyMsg> prec;
  auto fres = prec.get_future();
  std::thread dispatcher([&] {
    auto r = dispatch_plan(c, plan, corr);
    if (r) prec.set_value(r.value());
    else prec.set_value(ClientReplyMsg{});
  });

  // Sever the connection: kill the worker, so the coordinator sees a disconnect and must mark
  // the outcome UNKNOWN rather than inventing success.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  worker.stop(1);
  auto reply = fres.get();
  dispatcher.join();
  CHECK(!reply.committed);       // must NOT commit
  CHECK(reply.ambiguous);        // OUTCOME_UNKNOWN
  coordinator.stop(1);
  std::filesystem::remove(persist);
}

int main(int argc, char** argv) {
  (void)argc;
  // The test exe lives at <build>/tests/<Config>; the project build root is two levels up.
  std::filesystem::path here = std::filesystem::path(argv[0]).parent_path();
  g_config = here.filename().string();
  g_build_root = here / ".." / "..";
  return tst::run_all();
}
