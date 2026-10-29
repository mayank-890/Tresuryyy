#include "trading/core/thread_pin.hpp"

#include <atomic>

#if defined(__linux__)
#include <pthread.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <sys/sysctl.h>
#endif

namespace trading::core {
namespace {

std::atomic<std::uint32_t> g_pin_warnings{0};

} // namespace

std::uint32_t pinThreadWarningCount() noexcept {
  return g_pin_warnings.load(std::memory_order_relaxed);
}

Result<void, PinThreadError> pinThread(int core) noexcept {
  if (core < 0) {
    return Result<void, PinThreadError>::err(PinThreadError::SystemError);
  }

#if defined(__linux__)
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(static_cast<unsigned>(core), &set);
  const int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
  if (rc != 0) {
    return Result<void, PinThreadError>::err(PinThreadError::SystemError);
  }
  return Result<void, PinThreadError>::ok();
#elif defined(__APPLE__)
  (void)core;
  g_pin_warnings.fetch_add(1, std::memory_order_relaxed);
  return Result<void, PinThreadError>::err(PinThreadError::Unsupported);
#else
  (void)core;
  g_pin_warnings.fetch_add(1, std::memory_order_relaxed);
  return Result<void, PinThreadError>::err(PinThreadError::Unsupported);
#endif
}

} // namespace trading::core
