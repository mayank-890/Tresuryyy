#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace trading::control::line_v1 {

/// JSON line protocol (v1) exchanged on the control UDS until gRPC is wired.
/// Field names align with `proto/control/v1/control.proto`.

enum class Command : std::uint8_t {
  Halt = 1,
  Resume = 2,
  CancelAll = 3,
  Kill = 4,
};

enum class Scope : std::uint8_t {
  Global = 1,
  Strategy = 2,
  Instrument = 3,
};

struct Message {
  Command command{};
  Scope scope{Scope::Global};
  std::string operator_ref;
  std::string session_token;
  bool confirmed{false};
  std::string scope_ref;
};

[[nodiscard]] std::string encode(const Message& message);

[[nodiscard]] std::optional<Message> decode(std::string_view line);

} // namespace trading::control::line_v1
