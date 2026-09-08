#include "nmc/coordinator.hpp"
#include "nmc/net.hpp"

#include <condition_variable>
#include <cstdio>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using namespace nmc;

namespace {

struct Pending {
  std::promise<ClientReplyMsg> promise;
  WorkerBootId boot;
};

struct Node {
  Coordinator coord;
  std::mutex mu;
  std::map<WorkerBootId, std::shared_ptr<TcpSocket>> workers;
  std::map<DispatchId, Pending> pending;
  std::string persist;
  bool running = true;

  void resolve_dispatch(DispatchId d, ClientReplyMsg reply) {
    std::promise<ClientReplyMsg> p;
    {
      std::lock_guard<std::mutex> lk(mu);
      auto it = pending.find(d);
      if (it == pending.end()) return;
      p = std::move(it->second.promise);
      pending.erase(it);
    }
    p.set_value(std::move(reply));
  }
  void resolve_disconnect(WorkerBootId boot) {
    std::map<DispatchId, std::promise<ClientReplyMsg>> to_set;
    {
      std::lock_guard<std::mutex> lk(mu);
      for (auto it = pending.begin(); it != pending.end();) {
        if (it->second.boot == boot) {
          to_set[it->first] = std::move(it->second.promise);
          it = pending.erase(it);
        } else { ++it; }
      }
      workers.erase(boot);
    }
    ClientReplyMsg reply;
    reply.ok = false;
    reply.ambiguous = true;
    reply.note = "worker disconnected; outcome unknown";
    for (auto& [d, p] : to_set) { (void)d; p.set_value(reply); }
  }
};

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

void send_payload(TcpSocket& sock, FrameType type, ByteWriter& w, std::uint64_t corr) {
  std::vector<std::uint8_t> payload = w.take();
  send_frame(sock, type, std::span<const std::uint8_t>(payload.data(), payload.size()), corr);
}

void reply_client(TcpSocket& sock, ClientReplyMsg reply, std::uint64_t corr) {
  ByteWriter w;
  encode_client_reply(w, reply);
  send_payload(sock, FrameType::CLIENT_REPLY, w, corr);
}

}  // namespace

int main(int argc, char** argv) {
  auto args = parse_args(argc, argv);
  std::uint16_t port = args.count("port") ? static_cast<std::uint16_t>(std::stoi(args["port"])) : 0;
  std::string persist = args.count("persist") ? args["persist"] : std::string();
  std::uint64_t epoch = args.count("epoch") ? std::stoull(args["epoch"]) : 1;

  auto init = net_init();
  if (!init) { std::fprintf(stderr, "net_init failed\n"); return 1; }

  Node node;
  node.persist = persist;
  node.coord.attach_coordinator(CoordinatorId::make());
  if (!persist.empty() && std::filesystem::exists(persist)) {
    auto lr = node.coord.load(persist);
    if (!lr) std::fprintf(stderr, "load failed: %s\n", lr.error().message.c_str());
  }
  if (epoch > 1) {
    while (node.coord.current_epoch().value() < epoch) node.coord.advance_epoch();
  }

  TcpListener listener;
  auto lr = listener.listen("127.0.0.1", port);
  if (!lr) { std::fprintf(stderr, "listen failed\n"); return 2; }
  std::printf("PORT %u\n", listener.bound_port());
  std::fflush(stdout);

  while (node.running) {
    auto acc = listener.accept();
    if (!acc) break;
    auto sock = std::make_shared<TcpSocket>(std::move(acc.value()));
    std::thread([&node, sock] {
      WorkerBootId boot;
      bool is_worker = false;
      while (true) {
        FrameReceive fr = recv_frame(*sock);
        if (fr.status == FrameReceive::Status::Disconnect) break;
        if (fr.status != FrameReceive::Status::Ok) break;
        ByteReader pr(fr.payload.data(), fr.payload.size());
        switch (fr.header.type) {
          case FrameType::HELLO: {
            if (auto m = decode_hello(pr)) {
              boot = m->worker_boot; is_worker = true;
              node.coord.bind_worker_session(m->worker, m->worker_boot, m->name);
              { std::lock_guard<std::mutex> lk(node.mu); node.workers[boot] = sock; }
            }
            break;
          }
          case FrameType::REGISTER_TARGET: {
            if (auto m = decode_register_target(pr)) { m->owner_boot = boot; node.coord.apply_register_target(*m); }
            break;
          }
          case FrameType::CAPABILITY: {
            if (auto m = decode_capability(pr)) { if (m->publisher.is_null()) m->publisher = boot; node.coord.apply_capability(*m); }
            break;
          }
          case FrameType::DATA_EVIDENCE: {
            if (auto m = decode_data_evidence(pr)) { if (m->publisher.is_null()) m->publisher = boot; node.coord.apply_data_evidence(*m); }
            break;
          }
          case FrameType::HEARTBEAT: {
            if (auto m = decode_heartbeat(pr)) node.coord.apply_heartbeat(*m);
            break;
          }
          case FrameType::PING: {
            ByteWriter w; send_payload(*sock, FrameType::PONG, w, fr.header.correlation);
            break;
          }
          case FrameType::RESULT: {
            if (auto m = decode_result(pr)) {
              auto res = node.coord.apply_result(*m);
              ClientReplyMsg reply;
              reply.plan = m->plan;
              if (res) {
                reply.ok = true;
                reply.committed = res.value().committed;
                reply.ambiguous = res.value().ambiguous;
                reply.verification = res.value().verification;
                reply.output_bytes = res.value().output_bytes;
                reply.output_digest = res.value().output_digest;
                reply.note = res.value().note;
                if (reply.committed && !node.persist.empty()) node.coord.save(node.persist);
              } else {
                reply.ok = false;
                reply.note = res.error().message;
              }
              node.resolve_dispatch(m->dispatch, std::move(reply));
            }
            break;
          }
          case FrameType::CLIENT_REGISTER_OPERATION: {
            ClientReplyMsg reply;
            if (auto m = decode_client_register_op(pr)) {
              auto r = node.coord.create_operation(m->op);
              reply.ok = r.has_value();
            } else { reply.ok = false; }
            reply_client(*sock, std::move(reply), fr.header.correlation);
            break;
          }
          case FrameType::CLIENT_PLAN: {
            ClientReplyMsg reply;
            if (auto m = decode_client_plan(pr)) {
              auto out = node.coord.plan(m->op);
              if (out) {
                reply.ok = true;
                reply.created = out.value().created;
                reply.decision = out.value().decision;
                if (out.value().created) reply.plan = out.value().plan.id;
                reply.note = out.value().explanation.empty() ? "" : out.value().explanation.back();
              } else { reply.ok = false; reply.note = out.error().message; }
            } else { reply.ok = false; }
            reply_client(*sock, std::move(reply), fr.header.correlation);
            break;
          }
          case FrameType::CLIENT_DISPATCH: {
            ClientReplyMsg reply;
            auto md = decode_client_dispatch(pr);
            if (!md) { reply.note = "bad dispatch request"; reply_client(*sock, std::move(reply), fr.header.correlation); break; }
            ExecutionPlanId plan_id = md->plan;
            reply.plan = plan_id;
            DispatchId d = DispatchId::make(); AttemptId a = AttemptId::make();
            std::shared_ptr<TcpSocket> target_sock;
            if (node.coord.reserve(plan_id)) {
              auto dop = node.coord.prepare_dispatch(plan_id, d, a);
              if (dop) {
                const DispatchMsg& dm = dop.value().message;
                {
                  std::lock_guard<std::mutex> lk(node.mu);
                  auto it = node.workers.find(dm.worker_boot);
                  if (it != node.workers.end()) target_sock = it->second;
                }
                if (!target_sock) {
                  reply.note = "no worker connected";
                  node.coord.cancel_plan(plan_id);
                } else if (node.coord.confirm_dispatched(plan_id, d)) {
                  // Register the pending promise BEFORE sending so a fast RESULT cannot be lost.
                  std::promise<ClientReplyMsg> p;
                  auto fut = p.get_future();
                  { std::lock_guard<std::mutex> lk(node.mu); node.pending[d] = Pending{std::move(p), dm.worker_boot}; }
                  ByteWriter w; encode_dispatch(w, dm);
                  auto sr = send_frame(*target_sock, FrameType::DISPATCH, w.buffer(), 0);
                  if (!sr) {
                    ClientReplyMsg err; err.ok = false; err.ambiguous = true; err.plan = plan_id; err.note = "send dispatch failed";
                    node.resolve_dispatch(d, std::move(err));
                    node.coord.cancel_plan(plan_id);
                  }
                  reply = fut.get();  // no timeout: resolved by result or worker disconnect
                } else {
                  reply.note = "confirm dispatched failed";
                  node.coord.cancel_plan(plan_id);
                }
              } else {
                reply.note = "dispatch not reserved / stale";
              }
            } else {
              reply.note = "reserve failed";
            }
            reply_client(*sock, std::move(reply), fr.header.correlation);
            break;
          }
          case FrameType::CLIENT_GET_RESULT: {
            ClientReplyMsg reply;
            if (auto m = decode_client_get_result(pr)) {
              if (node.coord.has_result(m->plan)) {
                ExecutionResult res = node.coord.result_of(m->plan);
                reply.ok = true;
                reply.plan = res.plan;
                reply.committed = res.committed;
                reply.ambiguous = res.ambiguous;
                reply.verification = res.verification;
                reply.output_bytes = res.output_bytes;
                reply.output_digest = res.output_digest;
                reply.note = res.note;
              } else { reply.ok = false; reply.note = "no result"; }
            } else { reply.ok = false; }
            reply_client(*sock, std::move(reply), fr.header.correlation);
            break;
          }
          case FrameType::SHUTDOWN:
            node.running = false;
            break;
          default:
            break;
        }
      }
      if (is_worker) {
        node.coord.on_worker_disconnected(boot);
        node.resolve_disconnect(boot);
      }
      sock->close();
    }).detach();
  }

  net_cleanup();
  return 0;
}
