#include "trading/core/allocation_hook.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace trading::core {
namespace {

std::atomic<bool> g_tracking_enabled{false};
std::atomic<std::int64_t> g_allocation_count{0};
thread_local int g_guard_depth = 0;

} // namespace

void record_allocation(std::size_t /*bytes*/) noexcept {
  g_allocation_count.fetch_add(1, std::memory_order_relaxed);
}

void record_deallocation(std::size_t /*bytes*/) noexcept {
  g_allocation_count.fetch_add(1, std::memory_order_relaxed);
}

bool allocation_tracking_enabled() noexcept {
  return g_tracking_enabled.load(std::memory_order_relaxed);
}

AllocationGuard::AllocationGuard() noexcept {
  if (g_guard_depth++ == 0) {
    g_tracking_enabled.store(true, std::memory_order_release);
    reset();
  }
}

AllocationGuard::~AllocationGuard() noexcept {
  if (--g_guard_depth == 0) {
    g_tracking_enabled.store(false, std::memory_order_release);
  }
}

std::int64_t AllocationGuard::count() noexcept {
  return g_allocation_count.load(std::memory_order_relaxed);
}

void AllocationGuard::reset() noexcept {
  g_allocation_count.store(0, std::memory_order_relaxed);
}

} // namespace trading::core

void* operator new(std::size_t size) {
  void* ptr = std::malloc(size);
  if (ptr != nullptr && trading::core::allocation_tracking_enabled()) {
    trading::core::record_allocation(size);
  }
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  return ptr;
}

void operator delete(void* ptr) noexcept {
  if (ptr != nullptr && trading::core::allocation_tracking_enabled()) {
    trading::core::record_deallocation(0);
  }
  std::free(ptr);
}

void operator delete(void* ptr, std::size_t size) noexcept {
  if (ptr != nullptr && trading::core::allocation_tracking_enabled()) {
    trading::core::record_deallocation(size);
  }
  std::free(ptr);
}
