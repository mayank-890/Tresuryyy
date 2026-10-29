#pragma once

#include "errors.hpp"
#include "types.hpp"

#include "trading/core/result.hpp"

#include <filesystem>

namespace trading::instruments {

struct LoadOptions {
  bool require_sample_banner{true};
};

[[nodiscard]] trading::core::Result<InstrumentMaster, InstrumentError>
load_instrument_master_file(const std::filesystem::path& path, LoadOptions options = {});

[[nodiscard]] const Instrument* find_by_symbol(const InstrumentMaster& master,
                                               std::string_view symbol);

} // namespace trading::instruments
