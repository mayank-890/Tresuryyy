#include "calendar_load.hpp"

#include "parse_util.hpp"
#include "validate.hpp"

#include <fstream>
#include <toml++/toml.h>

namespace trading::calendar {

namespace {

trading::core::Result<Calendar, CalendarError> io_error(std::string message) {
  return trading::core::Result<Calendar, CalendarError>::err(
      CalendarError{CalendarErrorCode::Io, std::move(message)});
}

trading::core::Result<Calendar, CalendarError> parse_error(std::string message) {
  return trading::core::Result<Calendar, CalendarError>::err(
      CalendarError{CalendarErrorCode::Parse, std::move(message)});
}

std::vector<std::string> read_string_array(const toml::array& array) {
  std::vector<std::string> out;
  for (const auto& entry : array) {
    if (const auto* value = entry.as_string()) {
      out.push_back(value->get());
    }
  }
  return out;
}

} // namespace

trading::core::Result<Calendar, CalendarError>
load_calendar_file(const std::filesystem::path& path, LoadOptions options) {
  std::ifstream in(path);
  if (!in) {
    return io_error("cannot read " + path.string());
  }

  std::string first_line;
  if (!std::getline(in, first_line)) {
    return io_error("calendar file is empty: " + path.string());
  }
  if (options.require_sample_banner) {
    const auto banner = require_sample_banner(first_line);
    if (!banner) {
      return trading::core::Result<Calendar, CalendarError>::err(banner.error());
    }
  }

  in.clear();
  in.seekg(0);
  toml::table table;
  try {
    table = toml::parse(in);
  } catch (const toml::parse_error& error) {
    return parse_error(std::string("TOML parse error: ") + error.what());
  }

  Calendar calendar;
  if (const auto* meta = table["calendar"].as_table()) {
    if (const auto* name = (*meta)["name"].as_string()) {
      calendar.name = name->get();
    }
    if (const auto* timezone = (*meta)["timezone"].as_string()) {
      calendar.timezone = timezone->get();
    }
  }

  if (const auto* products = table["product"].as_array()) {
    for (const auto& entry : *products) {
      const auto* product_table = entry.as_table();
      if (product_table == nullptr) {
        continue;
      }
      ProductSchedule product;
      if (const auto* id = (*product_table)["id"].as_string()) {
        product.product_id = id->get();
      }
      if (const auto* sessions = (*product_table)["session"].as_array()) {
        for (const auto& session_entry : *sessions) {
          const auto* session_table = session_entry.as_table();
          if (session_table == nullptr) {
            continue;
          }
          SessionWindow window;
          if (const auto* open = (*session_table)["open"].as_string()) {
            const auto parsed = detail::parse_hms_since_midnight(open->get());
            if (!parsed) {
              return trading::core::Result<Calendar, CalendarError>::err(parsed.error());
            }
            window.open_since_midnight = parsed.value();
          }
          if (const auto* close = (*session_table)["close"].as_string()) {
            const auto parsed = detail::parse_hms_since_midnight(close->get());
            if (!parsed) {
              return trading::core::Result<Calendar, CalendarError>::err(parsed.error());
            }
            window.close_since_midnight = parsed.value();
          }
          product.sessions.push_back(window);
        }
      }
      calendar.products.push_back(std::move(product));
    }
  }

  if (const auto* holidays = table["holiday"].as_array()) {
    for (const auto& entry : *holidays) {
      const auto* holiday_table = entry.as_table();
      if (holiday_table == nullptr) {
        continue;
      }
      Holiday holiday;
      if (const auto* date = (*holiday_table)["date"].as_string()) {
        holiday.date_yyyy_mm_dd = date->get();
      }
      if (const auto* products = (*holiday_table)["products"].as_array()) {
        holiday.products = read_string_array(*products);
      }
      calendar.holidays.push_back(std::move(holiday));
    }
  }

  if (const auto* early_closes = table["early_close"].as_array()) {
    for (const auto& entry : *early_closes) {
      const auto* close_table = entry.as_table();
      if (close_table == nullptr) {
        continue;
      }
      EarlyClose early_close;
      if (const auto* date = (*close_table)["date"].as_string()) {
        early_close.date_yyyy_mm_dd = date->get();
      }
      if (const auto* close = (*close_table)["close"].as_string()) {
        const auto parsed = detail::parse_hms_since_midnight(close->get());
        if (!parsed) {
          return trading::core::Result<Calendar, CalendarError>::err(parsed.error());
        }
        early_close.close_since_midnight = parsed.value();
      }
      if (const auto* products = (*close_table)["products"].as_array()) {
        early_close.products = read_string_array(*products);
      }
      calendar.early_closes.push_back(std::move(early_close));
    }
  }

  if (const auto* maintenance = table["maintenance"].as_array()) {
    for (const auto& entry : *maintenance) {
      const auto* window_table = entry.as_table();
      if (window_table == nullptr) {
        continue;
      }
      MaintenanceWindow window;
      if (const auto* start = (*window_table)["start_utc"].as_string()) {
        const auto parsed = detail::parse_utc_timestamp(start->get());
        if (!parsed) {
          return trading::core::Result<Calendar, CalendarError>::err(parsed.error());
        }
        window.start_utc = parsed.value();
      }
      if (const auto* end = (*window_table)["end_utc"].as_string()) {
        const auto parsed = detail::parse_utc_timestamp(end->get());
        if (!parsed) {
          return trading::core::Result<Calendar, CalendarError>::err(parsed.error());
        }
        window.end_utc = parsed.value();
      }
      if (const auto* products = (*window_table)["products"].as_array()) {
        window.products = read_string_array(*products);
      }
      calendar.maintenance.push_back(std::move(window));
    }
  }

  const auto validation = validate_calendar_structure(calendar);
  if (!validation) {
    return trading::core::Result<Calendar, CalendarError>::err(validation.error());
  }
  if (calendar.timezone != "UTC") {
    return parse_error("Phase 0 calendars must use timezone = \"UTC\"");
  }

  return trading::core::Result<Calendar, CalendarError>::ok(std::move(calendar));
}

} // namespace trading::calendar
