#include "calendar_load.hpp"
#include "instrument_load.hpp"
#include "session_query.hpp"
#include "validate.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

#ifndef TRADING_REPO_ROOT
#error "TRADING_REPO_ROOT must be defined for calendar/instrument tests"
#endif

namespace {

fs::path repo_root() { return fs::path(TRADING_REPO_ROOT); }

void write_file(const fs::path& path, std::string_view body) {
  std::ofstream out(path);
  ASSERT_TRUE(out.is_open());
  out << body;
}

std::chrono::sys_seconds utc(int year, int month, int day, int hour, int minute, int second) {
  const std::chrono::year_month_day ymd{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
                                        std::chrono::day{static_cast<unsigned>(day)}};
  const auto day_point = std::chrono::sys_days{ymd};
  return day_point + std::chrono::hours(hour) + std::chrono::minutes(minute) +
         std::chrono::seconds(second);
}

} // namespace

TEST(CalendarInstrumentTests, LoadsSampleConfigFilesFromRepo) {
  const auto calendar = trading::calendar::load_calendar_file(
      repo_root() / "config/calendars/crypto_24x7.toml");
  ASSERT_TRUE(calendar);
  EXPECT_EQ(calendar.value().name, "crypto_24x7");

  const auto master = trading::instruments::load_instrument_master_file(
      repo_root() / "config/instruments/delta_crypto.toml");
  ASSERT_TRUE(master);
  const auto* btc =
      trading::instruments::find_by_symbol(master.value(), "SAMPLE-BTC-PERP");
  ASSERT_NE(btc, nullptr);
  EXPECT_EQ(btc->lot_size, 1);
  EXPECT_EQ(btc->tick_size, 100);
}

TEST(CalendarInstrumentTests, RejectsOverlappingSessions) {
  const fs::path path =
      fs::temp_directory_path() / "trading_calendar_overlap_test.toml";
  write_file(path, R"(# SAMPLE — NOT FOR TRADING
[calendar]
name = "overlap"
timezone = "UTC"

[[product]]
id = "P1"

[[product.session]]
open = "09:00:00"
close = "12:00:00"

[[product.session]]
open = "11:00:00"
close = "13:00:00"
)");
  const auto loaded = trading::calendar::load_calendar_file(path);
  ASSERT_FALSE(loaded);
  EXPECT_EQ(loaded.error().code, trading::calendar::CalendarErrorCode::Validation);
}

TEST(CalendarInstrumentTests, SessionQueryHonorsBoundariesWithFixedClock) {
  const fs::path path =
      fs::temp_directory_path() / "trading_calendar_boundary_test.toml";
  write_file(path, R"(# SAMPLE — NOT FOR TRADING
[calendar]
name = "bounded"
timezone = "UTC"

[[product]]
id = "P1"

[[product.session]]
open = "09:00:00"
close = "17:00:00"

[[holiday]]
date = "2026-01-01"
products = ["P1"]

[[early_close]]
date = "2026-12-24"
close = "13:00:00"
products = ["P1"]

[[maintenance]]
start_utc = "2026-06-15T02:00:00Z"
end_utc = "2026-06-15T04:00:00Z"
products = ["P1"]
)");
  const auto loaded = trading::calendar::load_calendar_file(path);
  ASSERT_TRUE(loaded);

  const auto& cal = loaded.value();
  const auto before_open =
      trading::calendar::query_session(cal, "P1", utc(2026, 3, 17, 8, 59, 0));
  EXPECT_EQ(before_open.state, trading::calendar::SessionState::OutOfSession);

  const auto open =
      trading::calendar::query_session(cal, "P1", utc(2026, 3, 17, 9, 0, 0));
  EXPECT_EQ(open.state, trading::calendar::SessionState::InSession);

  const auto holiday =
      trading::calendar::query_session(cal, "P1", utc(2026, 1, 1, 12, 0, 0));
  EXPECT_EQ(holiday.state, trading::calendar::SessionState::OutOfSession);
  EXPECT_EQ(holiday.reason, "holiday");

  const auto maintenance =
      trading::calendar::query_session(cal, "P1", utc(2026, 6, 15, 3, 0, 0));
  EXPECT_EQ(maintenance.state, trading::calendar::SessionState::OutOfSession);
  EXPECT_EQ(maintenance.reason, "maintenance window");

  const auto early_close =
      trading::calendar::query_session(cal, "P1", utc(2026, 12, 24, 13, 30, 0));
  EXPECT_EQ(early_close.state, trading::calendar::SessionState::OutOfSession);
}

TEST(CalendarInstrumentTests, InstrumentValidatorRejectsInvalidRows) {
  const fs::path path =
      fs::temp_directory_path() / "trading_instrument_invalid_test.toml";
  write_file(path, R"(# SAMPLE — NOT FOR TRADING

[[instrument]]
symbol = "DUP"
venue = "sample"
lot_size = 1
tick_size = 1
underlying = "X"

[[instrument]]
symbol = "DUP"
venue = "sample"
lot_size = 0
tick_size = -1
underlying = "X"
)");
  const auto loaded = trading::instruments::load_instrument_master_file(path);
  ASSERT_FALSE(loaded);
  EXPECT_EQ(loaded.error().code, trading::instruments::InstrumentErrorCode::Validation);
}

TEST(CalendarInstrumentTests, LookupBySymbolReturnsNullWhenMissing) {
  const auto master = trading::instruments::load_instrument_master_file(
      repo_root() / "config/instruments/delta_crypto.toml");
  ASSERT_TRUE(master);
  EXPECT_EQ(trading::instruments::find_by_symbol(master.value(), "MISSING"), nullptr);
}
