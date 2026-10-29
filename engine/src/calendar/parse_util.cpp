#include "parse_util.hpp"

#include <charconv>
#include <sstream>

namespace trading::calendar::detail {

namespace {

trading::core::Result<std::chrono::seconds, CalendarError> parse_error(std::string message) {
  return trading::core::Result<std::chrono::seconds, CalendarError>::err(
      CalendarError{CalendarErrorCode::Parse, std::move(message)});
}

} // namespace

trading::core::Result<std::chrono::seconds, CalendarError>
parse_hms_since_midnight(std::string_view text) {
  int hour = 0;
  int minute = 0;
  int second = 0;
  const char delimiter = ':';
  std::string buffer(text);
  std::istringstream stream(buffer);
  if (!(stream >> hour) || stream.peek() != delimiter || !(stream.ignore() >> minute) ||
      stream.peek() != delimiter || !(stream.ignore() >> second) || stream.peek() != EOF) {
    return parse_error("expected HH:MM:SS time, got '" + std::string(text) + "'");
  }
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
    return parse_error("time out of range: '" + std::string(text) + "'");
  }
  return trading::core::Result<std::chrono::seconds, CalendarError>::ok(
      std::chrono::hours(hour) + std::chrono::minutes(minute) + std::chrono::seconds(second));
}

trading::core::Result<std::chrono::sys_days, CalendarError> parse_ymd(std::string_view text) {
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
    return trading::core::Result<std::chrono::sys_days, CalendarError>::err(
        CalendarError{CalendarErrorCode::Parse, "expected YYYY-MM-DD date"});
  }
  auto parse_part = [](std::string_view part) -> trading::core::Result<int, CalendarError> {
    int value = 0;
    const auto* begin = part.data();
    const auto* end = begin + part.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
      return trading::core::Result<int, CalendarError>::err(
          CalendarError{CalendarErrorCode::Parse, "invalid date component"});
    }
    return trading::core::Result<int, CalendarError>::ok(value);
  };

  const auto year = parse_part(text.substr(0, 4));
  if (!year) {
    return trading::core::Result<std::chrono::sys_days, CalendarError>::err(year.error());
  }
  const auto month = parse_part(text.substr(5, 2));
  if (!month) {
    return trading::core::Result<std::chrono::sys_days, CalendarError>::err(month.error());
  }
  const auto day = parse_part(text.substr(8, 2));
  if (!day) {
    return trading::core::Result<std::chrono::sys_days, CalendarError>::err(day.error());
  }

  std::chrono::year_month_day ymd{std::chrono::year{year.value()},
                                  std::chrono::month{static_cast<unsigned>(month.value())},
                                  std::chrono::day{static_cast<unsigned>(day.value())}};
  if (!ymd.ok()) {
    return trading::core::Result<std::chrono::sys_days, CalendarError>::err(
        CalendarError{CalendarErrorCode::Parse, "invalid calendar date"});
  }
  return trading::core::Result<std::chrono::sys_days, CalendarError>::ok(
      std::chrono::sys_days{ymd});
}

trading::core::Result<std::chrono::sys_seconds, CalendarError>
parse_utc_timestamp(std::string_view text) {
  if (text.size() < 20 || text.back() != 'Z') {
    return trading::core::Result<std::chrono::sys_seconds, CalendarError>::err(
        CalendarError{CalendarErrorCode::Parse, "expected UTC timestamp ending with Z"});
  }
  const auto date_part = parse_ymd(text.substr(0, 10));
  if (!date_part) {
    return trading::core::Result<std::chrono::sys_seconds, CalendarError>::err(date_part.error());
  }
  if (text[10] != 'T') {
    return trading::core::Result<std::chrono::sys_seconds, CalendarError>::err(
        CalendarError{CalendarErrorCode::Parse, "expected T separator in timestamp"});
  }
  const auto time_part = parse_hms_since_midnight(text.substr(11, 8));
  if (!time_part) {
    return trading::core::Result<std::chrono::sys_seconds, CalendarError>::err(time_part.error());
  }
  const auto day_seconds = date_part.value();
  return trading::core::Result<std::chrono::sys_seconds, CalendarError>::ok(
      day_seconds + time_part.value());
}

} // namespace trading::calendar::detail
