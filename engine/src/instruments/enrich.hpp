#pragma once

#include "errors.hpp"
#include "types.hpp"

#include "trading/core/result.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace trading::instruments {

struct EnrichmentReport {
  std::vector<std::string> mapped_symbols;
  std::vector<std::string> missing_symbols;
  std::vector<std::string> missing_instrument_id;
  InstrumentMaster subset;
};

[[nodiscard]] trading::core::Result<std::vector<std::string>, InstrumentError>
load_symbol_list_file(const std::filesystem::path& path);

[[nodiscard]] trading::core::Result<InstrumentMaster, InstrumentError>
merge_masters_by_symbol(const InstrumentMaster& base, const InstrumentMaster& patch);

[[nodiscard]] trading::core::Result<EnrichmentReport, InstrumentError>
build_enrichment_report(const InstrumentMaster& master,
                        const std::vector<std::string>& requested_symbols);

[[nodiscard]] trading::core::Result<void, InstrumentError>
write_instrument_master_toml(const InstrumentMaster& master,
                             const std::filesystem::path& path,
                             bool include_sample_banner = true);

} // namespace trading::instruments
