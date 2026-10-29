#pragma once

#include "trading/core/result.hpp"

namespace trading::core {

[[nodiscard]] Result<void, PinThreadError> pinThread(int core) noexcept;

[[nodiscard]] std::uint32_t pinThreadWarningCount() noexcept;

} // namespace trading::core
