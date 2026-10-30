#include "instrument_load.hpp"

#include "validate.hpp"

#include <fstream>
#include <toml++/toml.h>

namespace trading::instruments {

namespace {

trading::core::Result<InstrumentMaster, InstrumentError> io_error(std::string message) {
  return trading::core::Result<InstrumentMaster, InstrumentError>::err(
      InstrumentError{InstrumentErrorCode::Io, std::move(message)});
}

trading::core::Result<InstrumentMaster, InstrumentError> parse_error(std::string message) {
  return trading::core::Result<InstrumentMaster, InstrumentError>::err(
      InstrumentError{InstrumentErrorCode::Parse, std::move(message)});
}

OptionType parse_option_type(std::string_view text) {
  if (text == "call") {
    return OptionType::Call;
  }
  if (text == "put") {
    return OptionType::Put;
  }
  return OptionType::None;
}

void build_index(InstrumentMaster& master) {
  master.index_by_symbol.clear();
  master.index_by_instrument_id.clear();
  for (std::size_t i = 0; i < master.instruments.size(); ++i) {
    master.index_by_symbol.emplace(master.instruments[i].symbol, i);
    if (master.instruments[i].instrument_id.has_value()) {
      master.index_by_instrument_id.emplace(*master.instruments[i].instrument_id, i);
    }
  }
}

} // namespace

trading::core::Result<InstrumentMaster, InstrumentError>
load_instrument_master_file(const std::filesystem::path& path, LoadOptions options) {
  std::ifstream in(path);
  if (!in) {
    return io_error("cannot read " + path.string());
  }

  std::string first_line;
  if (!std::getline(in, first_line)) {
    return io_error("instrument file is empty: " + path.string());
  }
  if (options.require_sample_banner) {
    const auto banner = require_sample_banner(first_line);
    if (!banner) {
      return trading::core::Result<InstrumentMaster, InstrumentError>::err(banner.error());
    }
  }

  in.clear();
  in.seekg(0);
  toml::table table;
  try {
    table = toml::parse(in);
  } catch (const toml::parse_error& error) {
    return parse_error(std::string("TOML parse error: ") + error.what());
  }

  InstrumentMaster master;
  if (const auto* instruments = table["instrument"].as_array()) {
    for (const auto& entry : *instruments) {
      const auto* instrument_table = entry.as_table();
      if (instrument_table == nullptr) {
        continue;
      }
      Instrument instrument;
      if (const auto* instrument_id = (*instrument_table)["instrument_id"].as_integer()) {
        instrument.instrument_id =
            static_cast<std::uint32_t>(instrument_id->get());
      }
      if (const auto* symbol = (*instrument_table)["symbol"].as_string()) {
        instrument.symbol = symbol->get();
      }
      if (const auto* venue = (*instrument_table)["venue"].as_string()) {
        instrument.venue = venue->get();
      }
      if (const auto* lot_size = (*instrument_table)["lot_size"].as_integer()) {
        instrument.lot_size = lot_size->get();
      }
      if (const auto* tick_size = (*instrument_table)["tick_size"].as_integer()) {
        instrument.tick_size = tick_size->get();
      }
      if (const auto* expiry = (*instrument_table)["expiry"].as_string()) {
        instrument.expiry_yyyy_mm_dd = expiry->get();
      }
      if (const auto* strike = (*instrument_table)["strike"].as_integer()) {
        instrument.strike_ticks = strike->get();
      }
      if (const auto* option_type = (*instrument_table)["option_type"].as_string()) {
        instrument.option_type = parse_option_type(option_type->get());
      }
      if (const auto* underlying = (*instrument_table)["underlying"].as_string()) {
        instrument.underlying = underlying->get();
      }
      master.instruments.push_back(std::move(instrument));
    }
  }

  const auto validation = validate_master_structure(master);
  if (!validation) {
    return trading::core::Result<InstrumentMaster, InstrumentError>::err(validation.error());
  }
  build_index(master);
  return trading::core::Result<InstrumentMaster, InstrumentError>::ok(std::move(master));
}

const Instrument* find_by_symbol(const InstrumentMaster& master, std::string_view symbol) {
  const auto found = master.index_by_symbol.find(std::string(symbol));
  if (found == master.index_by_symbol.end()) {
    return nullptr;
  }
  return &master.instruments[found->second];
}

const Instrument* find_by_instrument_id(const InstrumentMaster& master,
                                        const std::uint32_t instrument_id) {
  const auto found = master.index_by_instrument_id.find(instrument_id);
  if (found == master.index_by_instrument_id.end()) {
    return nullptr;
  }
  return &master.instruments[found->second];
}

} // namespace trading::instruments
