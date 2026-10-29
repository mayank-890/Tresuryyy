#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace trading::calendar {

struct SessionWindow {
  std::chrono::seconds open_since_midnight{};
  std::chrono::seconds close_since_midnight{};
};

struct ProductSchedule {
  std::string product_id;
  std::vector<SessionWindow> sessions;
};

struct Holiday {
  std::string date_yyyy_mm_dd;
  std::vector<std::string> products;
};

struct EarlyClose {
  std::string date_yyyy_mm_dd;
  std::chrono::seconds close_since_midnight{};
  std::vector<std::string> products;
};

struct MaintenanceWindow {
  std::chrono::sys_seconds start_utc{};
  std::chrono::sys_seconds end_utc{};
  std::vector<std::string> products;
};

struct Calendar {
  std::string name;
  std::string timezone;
  std::vector<ProductSchedule> products;
  std::vector<Holiday> holidays;
  std::vector<EarlyClose> early_closes;
  std::vector<MaintenanceWindow> maintenance;
};

enum class SessionState : std::uint8_t { InSession, OutOfSession };

struct SessionQuery {
  SessionState state{SessionState::OutOfSession};
  std::string reason;
};

} // namespace trading::calendar
