#pragma once
// Minimal deterministic test harness. Tests run naturally; there are NO timeouts anywhere.
// On assertion failure a counter is recorded and the run continues, so a single pass surfaces
// all defects. The process returns non-zero if any check failed.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tst {

inline int& checks_failed() { static int n = 0; return n; }
inline int& checks_passed() { static int n = 0; return n; }

inline void record(bool ok, const char* expr, const char* file, int line) {
  if (ok) {
    ++checks_passed();
  } else {
    ++checks_failed();
    std::printf("  [FAIL] %s:%d  %s\n", file, line, expr);
    std::fflush(stdout);
  }
}

struct TestCase { std::string name; std::function<void()> fn; };

inline std::vector<TestCase>& tests() { static std::vector<TestCase> t; return t; }

struct Registrar {
  Registrar(const std::string& name, std::function<void()> fn) { tests().push_back({name, std::move(fn)}); }
  Registrar(const char* name, std::function<void()> fn) : Registrar(std::string(name), std::move(fn)) {}
};

inline int run_all() {
  for (auto& tc : tests()) {
    std::printf("=== %s ===\n", tc.name.c_str());
    std::fflush(stdout);
    int before_failed = checks_failed();
    tc.fn();
    std::printf("   (%d passed, %d failed)\n", checks_passed(), checks_failed() - before_failed);
    std::fflush(stdout);
  }
  std::printf("\nTOTAL: %d passed, %d failed\n", checks_passed(), checks_failed());
  std::fflush(stdout);
  return checks_failed() == 0 ? 0 : 1;
}

}  // namespace tst

#define NMC_TEST(name)                                        \
  static void name();                                         \
  static ::tst::Registrar reg_##name(#name, name);            \
  static void name()

#define CHECK(cond) ::tst::record(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::tst::record((a) == (b), #a " == " #b, __FILE__, __LINE__)
