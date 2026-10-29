#pragma once

#include "types.hpp"

#include <chrono>
#include <string_view>

namespace trading::calendar {

[[nodiscard]] SessionQuery query_session(const Calendar& calendar, std::string_view product_id,
                                         std::chrono::sys_seconds instant_utc);

} // namespace trading::calendar
