#include "trading/control/line_v1.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace trading::control::line_v1 {
namespace {

[[nodiscard]] const char* command_name(Command command) {
  switch (command) {
  case Command::Halt:
    return "HALT";
  case Command::Resume:
    return "RESUME";
  case Command::CancelAll:
    return "CANCEL_ALL";
  case Command::Kill:
    return "KILL";
  }
  return "UNSPECIFIED";
}

[[nodiscard]] std::optional<Command> parse_command(std::string_view name) {
  if (name == "HALT") {
    return Command::Halt;
  }
  if (name == "RESUME") {
    return Command::Resume;
  }
  if (name == "CANCEL_ALL") {
    return Command::CancelAll;
  }
  if (name == "KILL") {
    return Command::Kill;
  }
  return std::nullopt;
}

[[nodiscard]] const char* scope_name(Scope scope) {
  switch (scope) {
  case Scope::Global:
    return "GLOBAL";
  case Scope::Strategy:
    return "STRATEGY";
  case Scope::Instrument:
    return "INSTRUMENT";
  }
  return "GLOBAL";
}

[[nodiscard]] std::optional<Scope> parse_scope(std::string_view name) {
  if (name == "GLOBAL") {
    return Scope::Global;
  }
  if (name == "STRATEGY") {
    return Scope::Strategy;
  }
  if (name == "INSTRUMENT") {
    return Scope::Instrument;
  }
  return std::nullopt;
}

} // namespace

std::string encode(const Message& message) {
  nlohmann::json body = {
      {"version", 1},
      {"command", command_name(message.command)},
      {"scope", scope_name(message.scope)},
      {"operator_ref", message.operator_ref},
      {"session_token", message.session_token},
      {"confirmed", message.confirmed},
      {"scope_ref", message.scope_ref},
  };
  return body.dump() + "\n";
}

std::optional<Message> decode(std::string_view line) {
  if (line.empty()) {
    return std::nullopt;
  }
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(line);
  } catch (const nlohmann::json::exception&) {
    return std::nullopt;
  }
  if (!body.is_object() || body.value("version", 0) != 1) {
    return std::nullopt;
  }
  if (!body.contains("command") || !body["command"].is_string()) {
    return std::nullopt;
  }
  const auto command = parse_command(body["command"].get<std::string>());
  if (!command) {
    return std::nullopt;
  }
  Message message;
  message.command = *command;
  if (body.contains("scope") && body["scope"].is_string()) {
    const auto scope = parse_scope(body["scope"].get<std::string>());
    message.scope = scope.value_or(Scope::Global);
  }
  const auto operator_json = body.find("operator_ref");
  if (operator_json == body.end() || !operator_json->is_string()) {
    return std::nullopt;
  }
  message.operator_ref = operator_json->get<std::string>();
  const auto token_json = body.find("session_token");
  if (token_json == body.end() || !token_json->is_string()) {
    return std::nullopt;
  }
  message.session_token = token_json->get<std::string>();
  message.confirmed = body.value("confirmed", false);
  const auto scope_ref_json = body.find("scope_ref");
  if (scope_ref_json != body.end() && scope_ref_json->is_string()) {
    message.scope_ref = scope_ref_json->get<std::string>();
  }
  if (message.operator_ref.size() > 32) {
    return std::nullopt;
  }
  return message;
}

} // namespace trading::control::line_v1
