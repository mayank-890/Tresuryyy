#pragma once

#include "trading/core/result.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace trading::broker::mock {

enum class ScenarioError : std::uint8_t {
  IoFailed = 1,
  ParseFailed = 2,
  UnsupportedVersion = 3,
};

enum class PlaceResponseKind : std::uint8_t {
  Ack = 1,
  Reject = 2,
  Ambiguous = 3,
  Timeout = 4,
  RateLimited = 5,
};

struct ScheduledFill {
  std::int64_t delay_from_place_ns{0};
  std::int32_t qty_lots{0};
  std::int64_t price_ticks{0};
};

struct PlaceStep {
  PlaceResponseKind response{PlaceResponseKind::Ack};
  std::string reason;
  std::uint64_t broker_order_id{0};
  std::vector<ScheduledFill> fills;
};

enum class TimelineActionKind : std::uint8_t {
  Disconnect = 1,
  Reconnect = 2,
};

struct TimelineAction {
  std::int64_t at_mono_ns{0};
  TimelineActionKind kind{TimelineActionKind::Disconnect};
};

struct MockScenario {
  std::uint32_t version{1};
  std::uint64_t seed{0};
  bool connect_succeed{true};
  std::vector<PlaceStep> place_steps;
  std::vector<TimelineAction> timeline;
};

[[nodiscard]] core::Result<MockScenario, ScenarioError> load_scenario_from_file(std::string_view path);
[[nodiscard]] core::Result<MockScenario, ScenarioError>
parse_scenario_toml(std::string_view toml_text);

} // namespace trading::broker::mock
