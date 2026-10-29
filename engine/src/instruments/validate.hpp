#pragma once

#include "errors.hpp"
#include "types.hpp"

#include "trading/core/result.hpp"

#include <string_view>

namespace trading::instruments {

[[nodiscard]] trading::core::Result<void, InstrumentError>
require_sample_banner(std::string_view first_line);

[[nodiscard]] trading::core::Result<void, InstrumentError>
validate_master_structure(const InstrumentMaster& master);

} // namespace trading::instruments
