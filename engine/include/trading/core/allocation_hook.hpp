#pragma once

#include <cstddef>
#include <cstdint>

namespace trading::core {

class AllocationGuard {
public:
  AllocationGuard() noexcept;
  ~AllocationGuard() noexcept;

  AllocationGuard(const AllocationGuard&) = delete;
  AllocationGuard& operator=(const AllocationGuard&) = delete;

  [[nodiscard]] static std::int64_t count() noexcept;
  static void reset() noexcept;
};

void record_allocation(std::size_t bytes) noexcept;
void record_deallocation(std::size_t bytes) noexcept;
[[nodiscard]] bool allocation_tracking_enabled() noexcept;

} // namespace trading::core
