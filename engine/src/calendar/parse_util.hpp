#pragma once

#include "errors.hpp"

#include "trading/core/result.hpp"

#include <chrono>
#include <string_view>

namespace trading::calendar::detail {

[[nodiscard]] trading::core::Result<std::chrono::seconds, CalendarError>
parse_hms_since_midnight(std::string_view text);

[[nodiscard]] trading::core::Result<std::chrono::sys_seconds, CalendarError>
parse_utc_timestamp(std::string_view text);

[[nodiscard]] trading::core::Result<std::chrono::sys_days, CalendarError>
parse_ymd(std::string_view text);

} // namespace trading::calendar::detail
