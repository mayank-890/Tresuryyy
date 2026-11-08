#include "trading/broker/mock/scenario.hpp"

#include <fstream>
#include <sstream>

#include <toml++/toml.h>

namespace trading::broker::mock {

namespace {

[[nodiscard]] PlaceResponseKind parse_place_kind(const std::string_view text) {
  if (text == "ack") {
    return PlaceResponseKind::Ack;
  }
  if (text == "reject") {
    return PlaceResponseKind::Reject;
  }
  if (text == "ambiguous") {
    return PlaceResponseKind::Ambiguous;
  }
  if (text == "timeout") {
    return PlaceResponseKind::Timeout;
  }
  if (text == "rate_limited" || text == "429") {
    return PlaceResponseKind::RateLimited;
  }
  return PlaceResponseKind::Ambiguous;
}

[[nodiscard]] TimelineActionKind parse_timeline_kind(const std::string_view text) {
  if (text == "reconnect") {
    return TimelineActionKind::Reconnect;
  }
  return TimelineActionKind::Disconnect;
}

[[nodiscard]] bool read_connect_block(const toml::table& root, MockScenario& out) {
  if (const toml::table* connect = root["connect"].as_table()) {
    if (const auto succeed = (*connect)["succeed"].value<bool>()) {
      out.connect_succeed = *succeed;
    }
  }
  return true;
}

[[nodiscard]] bool read_place_steps(const toml::table& root, MockScenario& out) {
  const toml::array* steps = root["place"].as_array();
  if (steps == nullptr) {
    steps = root["place_step"].as_array();
  }
  if (steps == nullptr) {
    return true;
  }

  for (const auto& node : *steps) {
    const toml::table* step = node.as_table();
    if (step == nullptr) {
      return false;
    }

    PlaceStep place{};
    const std::string response = (*step)["response"].value_or("ack");
    place.response = parse_place_kind(response);
    place.reason = (*step)["reason"].value_or("");
    place.broker_order_id = static_cast<std::uint64_t>((*step)["broker_order_id"].value_or(0));

    if (const toml::array* fills = (*step)["fill_schedule"].as_array()) {
      for (const auto& fill_node : *fills) {
        const toml::table* fill = fill_node.as_table();
        if (fill == nullptr) {
          return false;
        }
        ScheduledFill scheduled{};
        scheduled.delay_from_place_ns = (*fill)["delay_ns"].value_or(0);
        scheduled.qty_lots = (*fill)["qty_lots"].value_or(0);
        scheduled.price_ticks = (*fill)["price_ticks"].value_or(0);
        place.fills.push_back(scheduled);
      }
    }

    out.place_steps.push_back(std::move(place));
  }
  return true;
}

[[nodiscard]] bool read_timeline(const toml::table& root, MockScenario& out) {
  const toml::array* timeline = root["timeline"].as_array();
  if (timeline == nullptr) {
    return true;
  }
  for (const auto& node : *timeline) {
    const toml::table* row = node.as_table();
    if (row == nullptr) {
      return false;
    }
    TimelineAction action{};
    action.at_mono_ns = (*row)["at_mono_ns"].value_or(0);
    action.kind = parse_timeline_kind((*row)["action"].value_or("disconnect"));
    out.timeline.push_back(action);
  }
  return true;
}

} // namespace

core::Result<MockScenario, ScenarioError> parse_scenario_toml(const std::string_view toml_text) {
  toml::table root;
  try {
    root = toml::parse(toml_text);
  } catch (const toml::parse_error&) {
    return core::Result<MockScenario, ScenarioError>::err(ScenarioError::ParseFailed);
  }

  MockScenario scenario{};
  scenario.version = static_cast<std::uint32_t>(root["version"].value_or(1));
  if (scenario.version != 1) {
    return core::Result<MockScenario, ScenarioError>::err(ScenarioError::UnsupportedVersion);
  }
  scenario.seed = static_cast<std::uint64_t>(root["seed"].value_or(0));

  if (!read_connect_block(root, scenario) || !read_place_steps(root, scenario) ||
      !read_timeline(root, scenario)) {
    return core::Result<MockScenario, ScenarioError>::err(ScenarioError::ParseFailed);
  }

  return core::Result<MockScenario, ScenarioError>::ok(std::move(scenario));
}

core::Result<MockScenario, ScenarioError> load_scenario_from_file(const std::string_view path) {
  std::ifstream in{std::string(path)};
  if (!in) {
    return core::Result<MockScenario, ScenarioError>::err(ScenarioError::IoFailed);
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return parse_scenario_toml(buffer.str());
}

} // namespace trading::broker::mock
