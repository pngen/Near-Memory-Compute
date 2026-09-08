#pragma once
// Retained process ownership for the distributed proof. Every child has a real PID and handle
// that is verified before and after termination; no helper discards the PROCESS_INFORMATION.

#include <windows.h>
#include <string>
#include <vector>

#include "nmc/expected.hpp"

namespace proc {

struct Child {
  PROCESS_INFORMATION pi{};
  HANDLE stdout_read = INVALID_HANDLE_VALUE;
  bool started = false;
  bool redirected = false;

  std::uint32_t pid() const noexcept { return pi.dwProcessId; }
  void terminate(std::uint32_t exit_code) noexcept { if (pi.hProcess) TerminateProcess(pi.hProcess, exit_code); }
  std::uint32_t wait() noexcept {
    if (!pi.hProcess) return 0;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    return code;
  }
  void close_handles() noexcept {
    if (pi.hProcess) { CloseHandle(pi.hProcess); pi.hProcess = nullptr; }
    if (pi.hThread) { CloseHandle(pi.hThread); pi.hThread = nullptr; }
    if (stdout_read != INVALID_HANDLE_VALUE) { CloseHandle(stdout_read); stdout_read = INVALID_HANDLE_VALUE; }
  }
  bool stop(std::uint32_t exit_code = 1) noexcept {
    if (!started) return false;
    terminate(exit_code);
    wait();
    close_handles();
    started = false;
    return true;
  }
  bool read_line(std::string& out) noexcept {
    out.clear();
    if (stdout_read == INVALID_HANDLE_VALUE) return false;
    char buf[1];
    while (true) {
      DWORD n = 0;
      if (!ReadFile(stdout_read, buf, 1, &n, nullptr)) return false;
      if (n == 0) return false;
      out.push_back(buf[0]);
      if (buf[0] == '\n') break;
    }
    return true;
  }
};

inline std::string quote_arg(const std::string& a) {
  if (a.find(' ') == std::string::npos && a.find('"') == std::string::npos) return a;
  std::string q = "\"";
  for (char c : a) { if (c == '"') q += "\\\""; else q += c; }
  q += "\"";
  return q;
}

inline nmc::Result<Child> spawn(const std::string& exe, const std::vector<std::string>& args, bool redirect = true) {
  Child c;
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE write_end = INVALID_HANDLE_VALUE;
  if (redirect) {
    if (!CreatePipe(&c.stdout_read, &write_end, &sa, 0)) return nmc::make_error("E_SPAWN_PIPE", "CreatePipe failed");
    SetHandleInformation(c.stdout_read, HANDLE_FLAG_INHERIT, 0);
  }
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  if (redirect) {
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  }
  std::string cmd = quote_arg(exe);
  for (const auto& a : args) cmd += " " + quote_arg(a);
  std::vector<char> cmdline(cmd.begin(), cmd.end());
  cmdline.push_back('\0');
  BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                           nullptr, nullptr, &si, &c.pi);
  if (redirect && write_end != INVALID_HANDLE_VALUE) CloseHandle(write_end);
  if (!ok) return nmc::make_error("E_SPAWN", "CreateProcess failed: " + std::to_string(GetLastError()));
  c.started = true;
  c.redirected = redirect;
  return c;
}

}  // namespace proc
