#include "trading/core/log_queue.hpp"

#include <chrono>
#include <cstring>

namespace trading::core {

LogQueue::LogQueue(SinkFn sink) noexcept : sink_(sink) {}

LogQueue::~LogQueue() { stop(); }

void LogQueue::start_drainer() {
  if (running_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  drainer_ = std::thread([this]() { drain_loop(); });
}

void LogQueue::stop() {
  if (!running_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  if (drainer_.joinable()) {
    drainer_.join();
  }
}

bool LogQueue::try_push(const LogRecord& record) noexcept {
  LogRecord copy = record;
  return ring_.try_push(std::move(copy));
}

void LogQueue::drain_loop() {
  while (running_.load(std::memory_order_acquire)) {
    if (auto item = ring_.try_pop()) {
      if (sink_ != nullptr) {
        sink_(*item);
      }
      sink_calls_.fetch_add(1, std::memory_order_relaxed);
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  }
  while (auto item = ring_.try_pop()) {
    if (sink_ != nullptr) {
      sink_(*item);
    }
    sink_calls_.fetch_add(1, std::memory_order_relaxed);
  }
}

} // namespace trading::core
