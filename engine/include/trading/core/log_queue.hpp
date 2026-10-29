#pragma once

#include "trading/core/spsc_ring.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>

namespace trading::core {

enum class LogLevel : std::uint8_t { Debug = 1, Info = 2, Warn = 3, Error = 4 };

struct LogRecord {
  LogLevel level{LogLevel::Info};
  std::uint16_t component_id{0};
  char message[128]{};
};

class LogQueue {
public:
  using SinkFn = void (*)(const LogRecord&);

  static constexpr std::size_t kCapacity = 4096;

  explicit LogQueue(SinkFn sink) noexcept;
  ~LogQueue();

  LogQueue(const LogQueue&) = delete;
  LogQueue& operator=(const LogQueue&) = delete;

  void start_drainer();
  void stop();

  [[nodiscard]] bool try_push(const LogRecord& record) noexcept;

  [[nodiscard]] std::size_t sink_call_count() const noexcept { return sink_calls_.load(std::memory_order_relaxed); }

private:
  void drain_loop();

  SinkFn sink_{nullptr};
  SpscRing<LogRecord, kCapacity> ring_{};
  std::thread drainer_;
  std::atomic<bool> running_{false};
  std::atomic<std::size_t> sink_calls_{0};
};

} // namespace trading::core
