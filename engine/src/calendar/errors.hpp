#pragma once

#include <string>

namespace trading::calendar {

enum class CalendarErrorCode : std::uint8_t {
  Io,
  Parse,
  Validation,
  MissingSampleBanner,
};

struct CalendarError {
  CalendarErrorCode code{};
  std::string message;
};

} // namespace trading::calendar
