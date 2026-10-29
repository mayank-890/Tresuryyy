#include "trading/core/allocation_hook.hpp"
#include "trading/core/clock.hpp"
#include "trading/core/log_queue.hpp"
#include "trading/core/object_pool.hpp"
#include "trading/core/spsc_ring.hpp"
#include "trading/core/thread_pin.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <cstring>
#include <optional>

namespace {

constexpr std::size_t kPrStressItems = 10'000'000;

} // namespace

TEST(Clock, MonotonicAndWallNonDecreasing) {
  const auto m0 = trading::core::Clock::monotonicNs();
  const auto w0 = trading::core::Clock::wallNsUtc();
  const auto m1 = trading::core::Clock::monotonicNs();
  const auto w1 = trading::core::Clock::wallNsUtc();
  EXPECT_LE(m0, m1);
  EXPECT_LE(w0, w1);
}

TEST(SpscRing, EmptyAndFullBoundaries) {
  trading::core::SpscRing<int, 4> ring;
  EXPECT_FALSE(ring.try_pop().has_value());
  EXPECT_TRUE(ring.try_push(1));
  EXPECT_TRUE(ring.try_push(2));
  EXPECT_TRUE(ring.try_push(3));
  EXPECT_FALSE(ring.try_push(4));
  EXPECT_EQ(ring.try_pop(), 1);
  EXPECT_EQ(ring.try_pop(), 2);
  EXPECT_EQ(ring.try_pop(), 3);
  EXPECT_FALSE(ring.try_pop().has_value());
}

TEST(SpscRing, Wraparound) {
  trading::core::SpscRing<std::uint64_t, 8> ring;
  for (std::uint64_t i = 0; i < 1000; ++i) {
    ASSERT_TRUE(ring.try_push(i));
    auto v = ring.try_pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, i);
  }
}

TEST(SpscRing, StressTenMillion) {
  trading::core::SpscRing<std::uint64_t, 1024> ring;
  std::atomic<bool> failed{false};
  std::thread producer([&]() {
    for (std::uint64_t i = 0; i < kPrStressItems; ++i) {
      while (!ring.try_push(i)) {
      }
    }
  });
  std::thread consumer([&]() {
    for (std::uint64_t i = 0; i < kPrStressItems; ++i) {
      std::optional<std::uint64_t> v;
      do {
        v = ring.try_pop();
      } while (!v.has_value());
      if (*v != i) {
        failed.store(true, std::memory_order_relaxed);
        break;
      }
    }
  });
  producer.join();
  consumer.join();
  EXPECT_FALSE(failed.load());
}

TEST(ObjectPool, AcquireReleaseAndExhaustion) {
  trading::core::ObjectPool<int, 2> pool;
  int* a = pool.acquire();
  int* b = pool.acquire();
  EXPECT_NE(a, nullptr);
  EXPECT_NE(b, nullptr);
  EXPECT_EQ(pool.acquire(), nullptr);
  pool.release(a);
  EXPECT_NE(pool.acquire(), nullptr);
}

TEST(ObjectPool, DoubleReleaseDetectedInDebug) {
  trading::core::ObjectPool<int, 2> pool;
  int* a = pool.acquire();
  ASSERT_NE(a, nullptr);
  pool.release(a);
  pool.release(a);
#if !defined(NDEBUG)
  EXPECT_TRUE(pool.double_release_detected());
#endif
}

TEST(AllocationHook, RingAndPoolHotPathZeroAllocations) {
  trading::core::AllocationGuard guard;
  trading::core::SpscRing<int, 16> ring;
  trading::core::ObjectPool<int, 8> pool;
  for (int i = 0; i < 10000; ++i) {
    (void)ring.try_push(i);
    (void)ring.try_pop();
  }
  int* p = pool.acquire();
  if (p != nullptr) {
    *p = 7;
    pool.release(p);
  }
  EXPECT_EQ(trading::core::AllocationGuard::count(), 0);
}

TEST(LogQueue, ProducerDrainsToSink) {
  trading::core::LogQueue queue([](const trading::core::LogRecord& /*record*/) {
    // stub sink
  });
  queue.start_drainer();
  trading::core::LogRecord rec{};
  rec.level = trading::core::LogLevel::Info;
  std::strncpy(rec.message, "x-event", sizeof(rec.message));
  ASSERT_TRUE(queue.try_push(rec));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  queue.stop();
  EXPECT_GE(queue.sink_call_count(), 1u);
}

TEST(ThreadPin, ReturnsResultOnPlatform) {
  const auto before = trading::core::pin_thread_warning_count();
  const auto result = trading::core::pin_thread(0);
#if defined(__linux__)
  EXPECT_TRUE(result.is_ok());
#else
  EXPECT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), trading::core::PinThreadError::Unsupported);
  EXPECT_GE(trading::core::pin_thread_warning_count(), before + 1);
#endif
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
