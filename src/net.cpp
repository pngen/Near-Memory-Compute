#include "nmc/net.hpp"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstring>
#include <mutex>
#include <string>

namespace nmc {

constexpr std::uint64_t kInvalidSocket = static_cast<std::uint64_t>(~0ull);

namespace {
std::once_flag g_wsa_once;
bool g_wsa_init_ok = false;

void wsa_init() {
  WSADATA d;
  g_wsa_init_ok = (WSAStartup(MAKEWORD(2, 2), &d) == 0);
}
}  // namespace

Result<void> net_init() {
  std::call_once(g_wsa_once, wsa_init);
  if (!g_wsa_init_ok) return make_error("E_NET_INIT", "WSAStartup failed");
  return Result<void>();
}

void net_cleanup() {
  if (g_wsa_init_ok) {
    WSACleanup();
    g_wsa_init_ok = false;
  }
}

TcpSocket::TcpSocket() = default;
TcpSocket::TcpSocket(std::uint64_t native) : handle_(native) {}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : handle_(other.handle_) { other.handle_ = kInvalidSocket; }
TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
  if (this != &other) {
    close();
    handle_ = other.handle_;
    other.handle_ = kInvalidSocket;
  }
  return *this;
}
TcpSocket::~TcpSocket() { close(); }

bool TcpSocket::valid() const noexcept { return handle_ != kInvalidSocket; }
void TcpSocket::close() noexcept {
  if (valid()) {
    SOCKET s = static_cast<SOCKET>(handle_);
    shutdown(s, SD_BOTH);
    closesocket(s);
    handle_ = kInvalidSocket;
  }
}
std::uint64_t TcpSocket::native_handle() const noexcept { return handle_; }

Result<void> TcpSocket::send_all(const void* data, std::size_t n) const {
  if (!valid()) return make_error("E_NET_SEND", "socket not valid");
  const char* p = static_cast<const char*>(data);
  std::size_t sent = 0;
  while (sent < n) {
    int chunk = static_cast<int>(n - sent > static_cast<std::size_t>(INT_MAX) ? INT_MAX : (n - sent));
    int r = ::send(static_cast<SOCKET>(handle_), p + sent, chunk, 0);
    if (r == SOCKET_ERROR) {
      int e = WSAGetLastError();
      if (e == WSAEINTR) continue;
      return make_error("E_NET_SEND", "send failed: " + std::to_string(e));
    }
    if (r == 0) return make_error("E_NET_SEND", "send returned 0 (connection closed)");
    sent += static_cast<std::size_t>(r);
  }
  return Result<void>();
}

Result<void> TcpSocket::recv_all(void* data, std::size_t n, bool& peer_closed) const {
  peer_closed = false;
  if (!valid()) return make_error("E_NET_RECV", "socket not valid");
  char* p = static_cast<char*>(data);
  std::size_t got = 0;
  while (got < n) {
    int chunk = static_cast<int>(n - got > static_cast<std::size_t>(INT_MAX) ? INT_MAX : (n - got));
    int r = ::recv(static_cast<SOCKET>(handle_), p + got, chunk, 0);
    if (r == 0) { peer_closed = true; got = 0; return make_error("E_NET_EOF", "peer closed"); }
    if (r == SOCKET_ERROR) {
      int e = WSAGetLastError();
      if (e == WSAEINTR) continue;
      if (e == WSAECONNRESET || e == WSAECONNABORTED) { peer_closed = true; return make_error("E_NET_EOF", "connection reset"); }
      return make_error("E_NET_RECV", "recv failed: " + std::to_string(e));
    }
    got += static_cast<std::size_t>(r);
  }
  return Result<void>();
}

TcpListener::TcpListener() = default;
TcpListener::TcpListener(TcpListener&& other) noexcept : handle_(other.handle_), port_(other.port_) {
  other.handle_ = kInvalidSocket; other.port_ = 0;
}
TcpListener& TcpListener::operator=(TcpListener&& other) noexcept {
  if (this != &other) { close(); handle_ = other.handle_; port_ = other.port_; other.handle_ = kInvalidSocket; other.port_ = 0; }
  return *this;
}
TcpListener::~TcpListener() { close(); }
void TcpListener::close() noexcept {
  if (handle_ != kInvalidSocket) { closesocket(static_cast<SOCKET>(handle_)); handle_ = kInvalidSocket; }
}
std::uint16_t TcpListener::bound_port() const noexcept { return port_; }

Result<void> TcpListener::listen(const std::string& host, std::uint16_t port, int backlog) {
  auto init = net_init();
  if (!init) return init.error();
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return make_error("E_NET_SOCKET", "socket failed");
  // Allow address reuse to make fast restart reliable.
  int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;
  if (!host.empty()) {
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
  }
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    closesocket(s);
    return make_error("E_NET_BIND", "bind failed: " + std::to_string(WSAGetLastError()));
  }
  if (::listen(s, backlog) == SOCKET_ERROR) {
    closesocket(s);
    return make_error("E_NET_LISTEN", "listen failed: " + std::to_string(WSAGetLastError()));
  }
  // Retrieve actual bound port.
  sockaddr_in lb; int lsz = sizeof(lb);
  if (getsockname(s, reinterpret_cast<sockaddr*>(&lb), &lsz) == 0) port_ = ntohs(lb.sin_port);
  handle_ = static_cast<std::uint64_t>(s);
  return Result<void>();
}

Result<TcpSocket> TcpListener::accept() {
  if (handle_ == kInvalidSocket) return make_error("E_NET_ACCEPT", "not listening");
  SOCKET c = ::accept(static_cast<SOCKET>(handle_), nullptr, nullptr);
  if (c == INVALID_SOCKET) return make_error("E_NET_ACCEPT", "accept failed: " + std::to_string(WSAGetLastError()));
  return TcpSocket(static_cast<std::uint64_t>(c));
}

Result<TcpSocket> tcp_connect(const std::string& host, std::uint16_t port) {
  auto init = net_init();
  if (!init) return init.error();
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return make_error("E_NET_SOCKET", "socket failed");
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    closesocket(s);
    return make_error("E_NET_ADDR", "invalid address");
  }
  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    closesocket(s);
    return make_error("E_NET_CONNECT", "connect failed: " + std::to_string(WSAGetLastError()));
  }
  return TcpSocket(static_cast<std::uint64_t>(s));
}

Result<void> send_frame(TcpSocket& sock, FrameType type, std::span<const std::uint8_t> payload,
                        std::uint64_t correlation) {
  if (payload.size() > kMaxFramePayload) return make_error("E_FRAME_OVERSIZE", "payload exceeds frame maximum");
  FrameHeader h;
  h.magic = kProtocolMagic;
  h.version = kProtocolVersion;
  h.type = type;
  h.payload_len = static_cast<std::uint32_t>(payload.size());
  h.correlation = correlation;
  // CRC over header fields EXCLUDING the checksum field, plus the payload. The receiver
  // recomputes over the same field set, so the checksum field itself is never part of the CRC.
  Crc32 c;
  c.update_u32(h.magic);
  c.update_u16(h.version);
  c.update_u16(static_cast<std::uint16_t>(h.type));
  c.update_u32(h.payload_len);
  c.update_u64(h.correlation);
  c.update(payload.data(), payload.size());
  h.checksum = c.digest();
  ByteWriter full;
  encode_header(full, h);
  full.write_raw(payload.data(), payload.size());
  std::vector<std::uint8_t> wire = full.take();
  return sock.send_all(wire.data(), wire.size());
}

FrameReceive recv_frame(TcpSocket& sock) {
  FrameReceive out;
  std::uint8_t hdr[kFrameHeaderBytes];
  bool peer_closed = false;
  auto r = sock.recv_all(hdr, kFrameHeaderBytes, peer_closed);
  if (peer_closed) { out.status = FrameReceive::Status::Disconnect; return out; }
  if (!r) { out.status = FrameReceive::Status::Disconnect; return out; }

  ByteReader hr(hdr, kFrameHeaderBytes);
  auto h = decode_header(hr);
  if (!h || h->magic != kProtocolMagic) { out.status = FrameReceive::Status::Malformed; return out; }
  if (h->version != kProtocolVersion) { out.status = FrameReceive::Status::Malformed; return out; }
  if (h->payload_len > kMaxFramePayload) { out.status = FrameReceive::Status::Oversized; return out; }

  out.header = *h;
  if (h->payload_len > 0) {
    out.payload.resize(h->payload_len);
    bool pc = false;
    auto rr = sock.recv_all(out.payload.data(), out.payload.size(), pc);
    if (pc) { out.status = FrameReceive::Status::Truncated; return out; }
    if (!rr) { out.status = FrameReceive::Status::Truncated; return out; }
  }
  // Verify checksum over the same field set (excluding the checksum field).
  Crc32 c;
  c.update_u32(out.header.magic);
  c.update_u16(out.header.version);
  c.update_u16(static_cast<std::uint16_t>(out.header.type));
  c.update_u32(out.header.payload_len);
  c.update_u64(out.header.correlation);
  c.update(out.payload.data(), out.payload.size());
  if (c.digest() != out.header.checksum) { out.status = FrameReceive::Status::Malformed; return out; }
  out.status = FrameReceive::Status::Ok;
  return out;
}

}  // namespace nmc
