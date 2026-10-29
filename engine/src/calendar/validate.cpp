#include "validate.hpp"

#include <algorithm>

namespace trading::calendar {

namespace {

trading::core::Result<void, CalendarError> validation_error(std::string message) {
  return trading::core::Result<void, CalendarError>::err(
      CalendarError{CalendarErrorCode::Validation, std::move(message)});
}

} // namespace

bool sessions_overlap(const SessionWindow& left, const SessionWindow& right) {
  const auto left_open = left.open_since_midnight.count();
  const auto left_close = left.close_since_midnight.count();
  const auto right_open = right.open_since_midnight.count();
  const auto right_close = right.close_since_midnight.count();
  if (left_open >= left_close || right_open >= right_close) {
    return true;
  }
  return left_open < right_close && right_open < left_close;
}

trading::core::Result<void, CalendarError> require_sample_banner(std::string_view first_line) {
  constexpr std::string_view kBanner = "# SAMPLE — NOT FOR TRADING";
  if (first_line.find(kBanner) == std::string_view::npos) {
    return trading::core::Result<void, CalendarError>::err(
        CalendarError{CalendarErrorCode::MissingSampleBanner,
                      "config file must contain banner: " + std::string(kBanner)});
  }
  return trading::core::Result<void, CalendarError>::ok();
}

trading::core::Result<void, CalendarError>
validate_calendar_structure(const Calendar& calendar) {
  if (calendar.name.empty()) {
    return validation_error("calendar.name is required");
  }
  if (calendar.timezone.empty()) {
    return validation_error("calendar.timezone is required");
  }
  if (calendar.products.empty()) {
    return validation_error("at least one [[product]] entry is required");
  }

  for (const auto& product : calendar.products) {
    if (product.product_id.empty()) {
      return validation_error("product.id is required");
    }
    if (product.sessions.empty()) {
      return validation_error("product requires at least one [[product.session]]");
    }
    for (const auto& session : product.sessions) {
      if (session.open_since_midnight >= session.close_since_midnight) {
        return validation_error("session open must be before close for product " +
                                product.product_id);
      }
    }
    for (std::size_t i = 0; i < product.sessions.size(); ++i) {
      for (std::size_t j = i + 1; j < product.sessions.size(); ++j) {
        if (sessions_overlap(product.sessions[i], product.sessions[j])) {
          return validation_error("overlapping sessions for product " + product.product_id);
        }
      }
    }
  }

  for (const auto& window : calendar.maintenance) {
    if (window.start_utc >= window.end_utc) {
      return validation_error("maintenance start_utc must be before end_utc");
    }
  }

  return trading::core::Result<void, CalendarError>::ok();
}

} // namespace trading::calendar
