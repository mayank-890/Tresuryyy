#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace trading::core {

template <typename T, std::size_t N> class ObjectPool {
public:
  ObjectPool() {
    for (std::size_t i = 0; i < N; ++i) {
      free_stack_[i] = N - 1 - i;
    }
    free_top_ = N;
  }

  [[nodiscard]] T* acquire() noexcept {
    if (free_top_ == 0) {
      return nullptr;
    }
    const std::size_t idx = free_stack_[--free_top_];
#ifndef NDEBUG
    if (live_[idx]) {
      double_release_detected_ = true;
      return nullptr;
    }
    live_[idx] = true;
#endif
    return &values_[idx];
  }

  void release(T* ptr) noexcept {
    if (ptr == nullptr) {
      return;
    }
    const std::size_t idx = static_cast<std::size_t>(ptr - values_.data());
    if (idx >= N) {
#ifndef NDEBUG
      double_release_detected_ = true;
#endif
      return;
    }
#ifndef NDEBUG
    if (!live_[idx]) {
      double_release_detected_ = true;
      return;
    }
    live_[idx] = false;
#endif
    free_stack_[free_top_++] = idx;
  }

  [[nodiscard]] bool double_release_detected() const noexcept {
#ifndef NDEBUG
    return double_release_detected_;
#else
    return false;
#endif
  }

  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

private:
  std::array<T, N> values_{};
  std::array<std::size_t, N> free_stack_{};
  std::size_t free_top_{0};
#ifndef NDEBUG
  std::array<bool, N> live_{};
  bool double_release_detected_{false};
#endif
};

} // namespace trading::core
