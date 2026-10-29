#pragma once

#include "errors.hpp"
#include "types.hpp"

#include "trading/core/result.hpp"

#include <filesystem>
#include <string_view>

namespace trading::calendar {

struct LoadOptions {
  bool require_sample_banner{true};
};

[[nodiscard]] trading::core::Result<Calendar, CalendarError>
load_calendar_file(const std::filesystem::path& path, LoadOptions options = {});

} // namespace trading::calendar
