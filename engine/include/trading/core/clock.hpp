#pragma once

#include <chrono>
#include <cstdint>

namespace trading::core {

class Clock {
public:
  using MonoClock = std::chrono::steady_clock;
  using WallClock = std::chrono::system_clock;

  [[nodiscard]] static std::int64_t monotonicNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(MonoClock::now().time_since_epoch())
        .count();
  }

  [[nodiscard]] static std::int64_t wallNsUtc() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(WallClock::now().time_since_epoch())
        .count();
  }
};

} // namespace trading::core
