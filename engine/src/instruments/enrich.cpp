#include "enrich.hpp"

#include "instrument_load.hpp"
#include "validate.hpp"

#include <fstream>
#include <unordered_set>

namespace trading::instruments {

namespace {

trading::core::Result<void, InstrumentError> enrich_io_error(std::string message) {
  return trading::core::Result<void, InstrumentError>::err(
      InstrumentError{InstrumentErrorCode::Io, std::move(message)});
}

std::string option_type_name(OptionType type) {
  switch (type) {
  case OptionType::Call:
    return "call";
  case OptionType::Put:
    return "put";
  case OptionType::None:
  default:
    return "";
  }
}

} // namespace

trading::core::Result<std::vector<std::string>, InstrumentError>
load_symbol_list_file(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    return trading::core::Result<std::vector<std::string>, InstrumentError>::err(
        InstrumentError{InstrumentErrorCode::Io, "cannot read " + path.string()});
  }
  std::vector<std::string> symbols;
  std::string line;
  while (std::getline(in, line)) {
    const auto start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
      continue;
    }
    const auto end = line.find_last_not_of(" \t\r\n");
    const std::string trimmed = line.substr(start, end - start + 1);
    if (trimmed.empty() || trimmed.starts_with("#")) {
      continue;
    }
    symbols.push_back(trimmed);
  }
  return trading::core::Result<std::vector<std::string>, InstrumentError>::ok(
      std::move(symbols));
}

trading::core::Result<InstrumentMaster, InstrumentError>
merge_masters_by_symbol(const InstrumentMaster& base, const InstrumentMaster& patch) {
  InstrumentMaster merged = base;
  for (const auto& patch_row : patch.instruments) {
    bool replaced = false;
    for (auto& row : merged.instruments) {
      if (row.symbol == patch_row.symbol) {
        row = patch_row;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      merged.instruments.push_back(patch_row);
    }
  }
  const auto validation = validate_master_structure(merged);
  if (!validation) {
    return trading::core::Result<InstrumentMaster, InstrumentError>::err(validation.error());
  }
  merged.index_by_symbol.clear();
  merged.index_by_instrument_id.clear();
  for (std::size_t i = 0; i < merged.instruments.size(); ++i) {
    merged.index_by_symbol.emplace(merged.instruments[i].symbol, i);
    if (merged.instruments[i].instrument_id.has_value()) {
      merged.index_by_instrument_id.emplace(*merged.instruments[i].instrument_id, i);
    }
  }
  return trading::core::Result<InstrumentMaster, InstrumentError>::ok(std::move(merged));
}

trading::core::Result<EnrichmentReport, InstrumentError>
build_enrichment_report(const InstrumentMaster& master,
                        const std::vector<std::string>& requested_symbols) {
  EnrichmentReport report;
  std::unordered_set<std::string> seen;
  for (const auto& symbol : requested_symbols) {
    if (!seen.insert(symbol).second) {
      continue;
    }
    const Instrument* row = find_by_symbol(master, symbol);
    if (row == nullptr) {
      report.missing_symbols.push_back(symbol);
      continue;
    }
    report.mapped_symbols.push_back(symbol);
    if (!row->instrument_id.has_value()) {
      report.missing_instrument_id.push_back(symbol);
    }
    report.subset.instruments.push_back(*row);
  }
  report.subset.index_by_symbol.clear();
  report.subset.index_by_instrument_id.clear();
  for (std::size_t i = 0; i < report.subset.instruments.size(); ++i) {
    report.subset.index_by_symbol.emplace(report.subset.instruments[i].symbol, i);
    if (report.subset.instruments[i].instrument_id.has_value()) {
      report.subset.index_by_instrument_id.emplace(*report.subset.instruments[i].instrument_id,
                                                   i);
    }
  }
  return trading::core::Result<EnrichmentReport, InstrumentError>::ok(std::move(report));
}

trading::core::Result<void, InstrumentError>
write_instrument_master_toml(const InstrumentMaster& master,
                             const std::filesystem::path& path,
                             const bool include_sample_banner) {
  const auto validation = validate_master_structure(master);
  if (!validation) {
    return trading::core::Result<void, InstrumentError>::err(validation.error());
  }

  std::ofstream out(path);
  if (!out) {
    return enrich_io_error("cannot write " + path.string());
  }
  if (include_sample_banner) {
    out << "# SAMPLE — NOT FOR TRADING\n\n";
  }
  for (const auto& row : master.instruments) {
    out << "[[instrument]]\n";
    if (row.instrument_id.has_value()) {
      out << "instrument_id = " << *row.instrument_id << "\n";
    }
    out << "symbol = \"" << row.symbol << "\"\n";
    out << "venue = \"" << row.venue << "\"\n";
    out << "lot_size = " << row.lot_size << "\n";
    out << "tick_size = " << row.tick_size << "\n";
    out << "underlying = \"" << row.underlying << "\"\n";
    if (row.expiry_yyyy_mm_dd.has_value()) {
      out << "expiry = \"" << *row.expiry_yyyy_mm_dd << "\"\n";
    }
    if (row.strike_ticks > 0) {
      out << "strike = " << row.strike_ticks << "\n";
    }
    const auto opt = option_type_name(row.option_type);
    if (!opt.empty()) {
      out << "option_type = \"" << opt << "\"\n";
    }
    out << "\n";
  }
  return trading::core::Result<void, InstrumentError>::ok();
}

} // namespace trading::instruments
