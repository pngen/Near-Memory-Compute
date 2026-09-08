#pragma once
// Minimal value-result type and error. The runtime never relies on exceptions for the control
// flow of expected outcomes; failures are explicit and inspected before mutation.

#include <string>
#include <utility>

namespace nmc {

struct Error {
  std::string code;
  std::string message;
  Error() = default;
  Error(std::string code_, std::string message_) : code(std::move(code_)), message(std::move(message_)) {}
  friend bool operator==(const Error& a, const Error& b) noexcept { return a.code == b.code; }
  friend bool operator!=(const Error& a, const Error& b) noexcept { return !(a == b); }
};

template <class T>
class Result {
 public:
  Result() : ok_(false), val_(), err_("E_UNEXPECTED", "no value") {}
  Result(T value) : ok_(true), val_(std::move(value)), err_() {}
  Result(Error err) : ok_(false), val_(), err_(std::move(err)) {}
  Result(const Result&) = default;
  Result(Result&&) noexcept = default;
  Result& operator=(const Result&) = default;
  Result& operator=(Result&&) noexcept = default;
  [[nodiscard]] bool has_value() const noexcept { return ok_; }
  [[nodiscard]] bool is_error() const noexcept { return !ok_; }
  explicit operator bool() const noexcept { return ok_; }
  T& value() & { return val_; }
  const T& value() const& { return val_; }
  T&& value() && { return std::move(val_); }
  const Error& error() const& { return err_; }
  Error& error() & { return err_; }
  T value_or(T fallback) const& { return ok_ ? val_ : std::move(fallback); }
 private:
  bool ok_;
  [[no_unique_address]] T val_{};
  [[no_unique_address]] Error err_{};
};

template <>
class Result<void> {
 public:
  Result() : ok_(true) {}
  Result(Error err) : ok_(false), err_(std::move(err)) {}
  [[nodiscard]] bool has_value() const noexcept { return ok_; }
  [[nodiscard]] bool is_error() const noexcept { return !ok_; }
  explicit operator bool() const noexcept { return ok_; }
  const Error& error() const& { return err_; }
  Error& error() & { return err_; }
 private:
  bool ok_;
  [[no_unique_address]] Error err_{};
};

inline Error make_error(std::string code, std::string message) { return Error{std::move(code), std::move(message)}; }

}  // namespace nmc
