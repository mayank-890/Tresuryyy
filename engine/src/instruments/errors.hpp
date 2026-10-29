#pragma once

#include <cstdint>
#include <string>

namespace trading::instruments {

enum class InstrumentErrorCode : std::uint8_t {
  Io,
  Parse,
  Validation,
  MissingSampleBanner,
};

struct InstrumentError {
  InstrumentErrorCode code{};
  std::string message;
};

} // namespace trading::instruments
