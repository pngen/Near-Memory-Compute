#include "nmc/persistence.hpp"

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "nmc/checksum.hpp"
#include "nmc/util.hpp"

// Windows file APIs for atomic replacement.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace nmc {

namespace {

Result<std::vector<std::uint8_t>> read_whole_file(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return make_error("E_PERSIST_OPEN", "cannot open persistence file");
  f.seekg(0, std::ios::end);
  std::streamoff size = f.tellg();
  if (size < 0) return make_error("E_PERSIST_SIZE", "cannot determine file size");
  if (static_cast<std::uint64_t>(size) > PersistentStore::kMaxPayload + 64) {
    return make_error("E_PERSIST_OVERSIZE", "persistence file exceeds maximum size");
  }
  f.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
  if (size > 0 && !f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size))) {
    return make_error("E_PERSIST_READ", "short read on persistence file");
  }
  return data;
}

// Envelope layout (little-endian over a ByteWriter):
//   magic(4) version(2) payload_len(8) payload(...) crc32(4)
// CRC is computed over magic, version, payload_len, and the payload.
struct Envelope {
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint64_t payload_len = 0;
  std::vector<std::uint8_t> payload;
  std::uint32_t crc = 0;
};

void compute_payload_crc(uint32_t magic, uint16_t version, uint64_t len,
                         const std::vector<std::uint8_t>& payload, uint32_t& out_crc) {
  Crc32 c;
  c.update_u32(magic);
  c.update_u16(version);
  c.update_u64(len);
  c.update(payload.data(), payload.size());
  out_crc = c.digest();
}

Result<std::vector<std::uint8_t>> build_envelope(const PersistentStore::EncodeFn& encode) {
  ByteWriter w;
  encode(w);
  std::vector<std::uint8_t> payload = w.take();
  if (payload.size() > PersistentStore::kMaxPayload) {
    return make_error("E_PERSIST_OVERSIZE", "serialized payload exceeds maximum size");
  }
  ByteWriter out;
  out.write_u32(PersistentStore::kMagic);
  out.write_u16(PersistentStore::kFormatVersion);
  out.write_u64(static_cast<std::uint64_t>(payload.size()));
  out.write_raw(payload.data(), payload.size());
  uint32_t crc = 0;
  compute_payload_crc(PersistentStore::kMagic, PersistentStore::kFormatVersion,
                      static_cast<std::uint64_t>(payload.size()), payload, crc);
  out.write_u32(crc);
  return out.take();
}

}  // namespace

Result<void> PersistentStore::save(const std::filesystem::path& path, const EncodeFn& encode) const {
  auto built = build_envelope(encode);
  if (!built) return built.error();

  const std::filesystem::path tmp = path.string() + ".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return make_error("E_PERSIST_WRITE", "cannot create temp persistence file");
  }
  const auto& bytes = built.value();
  bool ok = true;
  DWORD written = 0;
  if (!bytes.empty() &&
      (!WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
       written != bytes.size())) {
    ok = false;
  }
  if (ok) ok = (FlushFileBuffers(h) != FALSE);
  CloseHandle(h);
  if (!ok) {
    DeleteFileW(tmp.c_str());
    return make_error("E_PERSIST_WRITE", "failure writing temp persistence file");
  }
  // Atomically replace the destination.
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(tmp.c_str());
    return make_error("E_PERSIST_REPLACE", "atomic replace failed");
  }
  return Result<void>();
}

Result<void> PersistentStore::load(const std::filesystem::path& path, const DecodeFn& decode) const {
  auto raw = read_whole_file(path);
  if (!raw) return raw.error();

  ByteReader r(raw.value().data(), raw.value().size());
  Envelope env;
  if (!r.read_u32(env.magic)) return make_error("E_PERSIST_MAGIC", "truncated header");
  if (env.magic != kMagic) return make_error("E_PERSIST_MAGIC", "bad magic");
  if (!r.read_u16(env.version)) return make_error("E_PERSIST_VERSION", "truncated version");
  if (env.version != kFormatVersion) return make_error("E_PERSIST_VERSION", "unsupported format version");
  if (!r.read_u64(env.payload_len)) return make_error("E_PERSIST_LEN", "truncated payload length");
  if (env.payload_len > kMaxPayload) return make_error("E_PERSIST_OVERSIZE", "payload too large");
  if (env.payload_len > r.remaining()) return make_error("E_PERSIST_TRUNC", "payload truncated");

  std::span<const std::uint8_t> sp;
  if (!r.read_span(sp, static_cast<std::size_t>(env.payload_len))) {
    return make_error("E_PERSIST_TRUNC", "cannot read payload");
  }
  env.payload.assign(sp.begin(), sp.end());

  if (!r.read_u32(env.crc)) return make_error("E_PERSIST_CRC", "truncated checksum");
  if (!r.at_end()) return make_error("E_PERSIST_TRAILING", "trailing garbage after payload");

  uint32_t expected = 0;
  compute_payload_crc(env.magic, env.version, env.payload_len, env.payload, expected);
  if (expected != env.crc) return make_error("E_PERSIST_CRC", "checksum mismatch (corruption)");

  ByteReader payload_r(env.payload.data(), env.payload.size());
  if (!decode(payload_r)) return make_error("E_PERSIST_DECODE", "malformed persistence payload");
  if (!payload_r.at_end()) return make_error("E_PERSIST_TRAILING", "trailing garbage inside payload");
  return Result<void>();
}

}  // namespace nmc
