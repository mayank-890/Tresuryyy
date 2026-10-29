#include "session_query.hpp"

#include <algorithm>
#include <cstdio>

namespace trading::calendar {

namespace {

bool applies_to_product(const std::vector<std::string>& products, std::string_view product_id) {
  if (products.empty()) {
    return true;
  }
  return std::find(products.begin(), products.end(), product_id) != products.end();
}

const ProductSchedule* find_product(const Calendar& calendar, std::string_view product_id) {
  for (const auto& product : calendar.products) {
    if (product.product_id == product_id) {
      return &product;
    }
  }
  return nullptr;
}

std::chrono::seconds seconds_since_midnight_utc(std::chrono::sys_seconds instant_utc) {
  const auto day = std::chrono::floor<std::chrono::days>(instant_utc);
  return instant_utc - std::chrono::sys_seconds{day};
}

std::string format_day(std::chrono::sys_seconds instant_utc) {
  const auto day = std::chrono::floor<std::chrono::days>(instant_utc);
  const std::chrono::year_month_day ymd{std::chrono::sys_days{day}};
  const auto year = static_cast<int>(ymd.year());
  const unsigned month = static_cast<unsigned>(ymd.month());
  const unsigned d = static_cast<unsigned>(ymd.day());
  char buffer[11];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", year, month, d);
  return buffer;
}

} // namespace

SessionQuery query_session(const Calendar& calendar, std::string_view product_id,
                           std::chrono::sys_seconds instant_utc) {
  const ProductSchedule* product = find_product(calendar, product_id);
  if (product == nullptr) {
    return SessionQuery{SessionState::OutOfSession, "unknown product"};
  }

  for (const auto& window : calendar.maintenance) {
    if (!applies_to_product(window.products, product_id)) {
      continue;
    }
    if (instant_utc >= window.start_utc && instant_utc < window.end_utc) {
      return SessionQuery{SessionState::OutOfSession, "maintenance window"};
    }
  }

  const std::string day = format_day(instant_utc);
  for (const auto& holiday : calendar.holidays) {
    if (holiday.date_yyyy_mm_dd == day && applies_to_product(holiday.products, product_id)) {
      return SessionQuery{SessionState::OutOfSession, "holiday"};
    }
  }

  std::chrono::seconds effective_close = std::chrono::hours(24) - std::chrono::seconds(1);
  for (const auto& early_close : calendar.early_closes) {
    if (early_close.date_yyyy_mm_dd == day && applies_to_product(early_close.products, product_id)) {
      effective_close = early_close.close_since_midnight;
      break;
    }
  }

  const auto since_midnight = seconds_since_midnight_utc(instant_utc);
  for (const auto& session : product->sessions) {
    const auto close = std::min(session.close_since_midnight, effective_close);
    if (since_midnight >= session.open_since_midnight && since_midnight < close) {
      return SessionQuery{SessionState::InSession, "regular session"};
    }
  }

  return SessionQuery{SessionState::OutOfSession, "outside session hours"};
}

} // namespace trading::calendar
