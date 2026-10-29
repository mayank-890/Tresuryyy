#pragma once

#include "trading/core/result.hpp"

namespace trading::core {

[[nodiscard]] Result<void, PinThreadError> pin_thread(int core) noexcept;

[[nodiscard]] std::uint32_t pin_thread_warning_count() noexcept;

} // namespace trading::core
