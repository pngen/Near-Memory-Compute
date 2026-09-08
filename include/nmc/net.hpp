#pragma once
// Framed TCP transport for the reference deployment (coordinator <-> workers). Uses real OS
// processes and sockets. No socket I/O is performed while the coordinator state lock is held.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "nmc/expected.hpp"
#include "nmc/protocol.hpp"

namespace nmc {

// RAII wrapper over a native TCP socket.
class TcpSocket {
 public:
  TcpSocket();
  ~TcpSocket();
  TcpSocket(TcpSocket&& other) noexcept;
  TcpSocket& operator=(TcpSocket&& other) noexcept;
  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;

  friend Result<TcpSocket> tcp_connect(const std::string& host, std::uint16_t port);

  bool valid() const noexcept;
  void close() noexcept;
  std::uint64_t native_handle() const noexcept;

  // Send exactly n bytes. Returns error on partial send / failure.
  Result<void> send_all(const void* data, std::size_t n) const;
  // Receive exactly n bytes. Returns errors: EOF (peer closed), partial.
  Result<void> recv_all(void* data, std::size_t n, bool& peer_closed) const;

 private:
  explicit TcpSocket(std::uint64_t native);
  std::uint64_t handle_ = 0;  // INVALID_SOCKET == ~0
  friend class TcpListener;
};

class TcpListener {
 public:
  TcpListener();
  ~TcpListener();
  TcpListener(TcpListener&& other) noexcept;
  TcpListener& operator=(TcpListener&& other) noexcept;
  TcpListener(const TcpListener&) = delete;
  TcpListener& operator=(const TcpListener&) = delete;

  Result<void> listen(const std::string& host, std::uint16_t port, int backlog = 16);
  Result<TcpSocket> accept();
  std::uint16_t bound_port() const noexcept;
  void close() noexcept;

 private:
  std::uint64_t handle_ = 0;
  std::uint16_t port_ = 0;
};

// Connect a client socket to a host:port.
Result<TcpSocket> tcp_connect(const std::string& host, std::uint16_t port);

// Frame receive result. Disconnect means a clean peer EOF; malformed/oversized are protocol
// violations that must be reported (and typically cause the connection to be dropped).
struct FrameReceive {
  enum class Status { Ok, Disconnect, Malformed, Truncated, Oversized };
  Status status = Status::Ok;
  FrameHeader header;
  std::vector<std::uint8_t> payload;
};

// Send one frame (header + payload) with a CRC-32 integrity checksum.
Result<void> send_frame(TcpSocket& sock, FrameType type, std::span<const std::uint8_t> payload,
                        std::uint64_t correlation);

// Receive one frame, validating magic/version/type/payload length/checksum.
FrameReceive recv_frame(TcpSocket& sock);

// Global Winsock init. No-op after first call.
Result<void> net_init();
void net_cleanup();

}  // namespace nmc
