#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace trading::core {

template <typename T, std::size_t N> class SpscRing {
  static_assert(N >= 2, "SpscRing capacity must be at least 2");
  static_assert((N & (N - 1)) == 0, "SpscRing capacity must be a power of two");
  static_assert(std::is_nothrow_move_constructible_v<T>, "SpscRing element must be nothrow move constructible");

public:
  SpscRing() = default;

  [[nodiscard]] bool try_push(T value) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed); // producer-local read
    const std::size_t next = (head + 1) & kMask;
    const std::size_t tail = tail_.load(std::memory_order_acquire); // observe consumer progress
    if (next == tail) {
      return false;
    }
    slots_[head] = std::move(value);
    head_.store(next, std::memory_order_release); // publish element
    return true;
  }

  [[nodiscard]] std::optional<T> try_pop() noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed); // consumer-local read
    const std::size_t head = head_.load(std::memory_order_acquire); // observe producer progress
    if (tail == head) {
      return std::nullopt;
    }
    T value = std::move(slots_[tail]);
    tail_.store((tail + 1) & kMask, std::memory_order_release); // publish consumption
    return value;
  }

  [[nodiscard]] std::size_t size_approx() const noexcept {
    const std::size_t head = head_.load(std::memory_order_acquire);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    return (head + N - tail) & kMask;
  }

  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N - 1; }

private:
  static constexpr std::size_t kMask = N - 1;

  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
  T slots_[N]{};
};

} // namespace trading::core
