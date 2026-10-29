// Plain std::chrono baseline (Google Benchmark not in vcpkg for T-0005).
#include "trading/core/spsc_ring.hpp"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <thread>

int main() {
  trading::core::SpscRing<std::uint64_t, 4096> ring;
  constexpr std::uint64_t kItems = 5'000'000;
  const auto start = std::chrono::steady_clock::now();
  std::thread producer([&]() {
    for (std::uint64_t i = 0; i < kItems; ++i) {
      while (!ring.try_push(i)) {
      }
    }
  });
  std::thread consumer([&]() {
    for (std::uint64_t i = 0; i < kItems; ++i) {
      while (!ring.try_pop().has_value()) {
      }
    }
  });
  producer.join();
  consumer.join();
  const auto end = std::chrono::steady_clock::now();
  const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  const double sec = static_cast<double>(ns) / 1e9;
  const double items_per_sec = static_cast<double>(kItems) / sec;
  std::printf("spsc_items=%llu elapsed_ns=%lld throughput_items_per_sec=%.0f\n",
              static_cast<unsigned long long>(kItems), static_cast<long long>(ns), items_per_sec);
  return 0;
}
