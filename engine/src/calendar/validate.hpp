#pragma once

#include "errors.hpp"
#include "types.hpp"

#include "trading/core/result.hpp"

#include <string_view>

namespace trading::calendar {

[[nodiscard]] trading::core::Result<void, CalendarError>
validate_calendar_structure(const Calendar& calendar);

[[nodiscard]] bool sessions_overlap(const SessionWindow& left, const SessionWindow& right);

[[nodiscard]] trading::core::Result<void, CalendarError>
require_sample_banner(std::string_view first_line);

} // namespace trading::calendar
