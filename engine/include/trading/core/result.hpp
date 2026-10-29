#pragma once

#include <cstdint>
#include <utility>
#include <variant>

namespace trading::core {

enum class PinThreadError : std::uint8_t { Unsupported, SystemError };

template <typename T, typename E> class [[nodiscard]] Result {
public:
  static Result ok(T value) { return Result(std::in_place_index<0>, std::move(value)); }

  static Result err(E error) { return Result(std::in_place_index<1>, error); }

  [[nodiscard]] bool is_ok() const { return data_.index() == 0; }
  [[nodiscard]] explicit operator bool() const { return is_ok(); }

  T& value() { return std::get<0>(data_); }
  const T& value() const { return std::get<0>(data_); }
  E error() const { return std::get<1>(data_); }

private:
  template <typename... Args>
  explicit Result(std::in_place_index_t<0>, Args&&... args)
      : data_(std::in_place_index<0>, std::forward<Args>(args)...) {}

  explicit Result(std::in_place_index_t<1>, E error) : data_(std::in_place_index<1>, error) {}

  std::variant<T, E> data_;
};

template <typename E> class [[nodiscard]] Result<void, E> {
public:
  static Result ok() {
    Result r;
    r.ok_ = true;
    return r;
  }

  static Result err(E error) {
    Result r;
    r.ok_ = false;
    r.error_ = error;
    return r;
  }

  [[nodiscard]] bool is_ok() const { return ok_; }
  [[nodiscard]] explicit operator bool() const { return ok_; }
  E error() const { return error_; }

private:
  bool ok_{false};
  E error_{};
};

} // namespace trading::core
