#include "validate.hpp"

#include <unordered_set>

namespace trading::instruments {

namespace {

trading::core::Result<void, InstrumentError> validation_error(std::string message) {
  return trading::core::Result<void, InstrumentError>::err(
      InstrumentError{InstrumentErrorCode::Validation, std::move(message)});
}

} // namespace

trading::core::Result<void, InstrumentError> require_sample_banner(std::string_view first_line) {
  constexpr std::string_view kBanner = "# SAMPLE — NOT FOR TRADING";
  if (first_line.find(kBanner) == std::string_view::npos) {
    return trading::core::Result<void, InstrumentError>::err(
        InstrumentError{InstrumentErrorCode::MissingSampleBanner,
                        "config file must contain banner: " + std::string(kBanner)});
  }
  return trading::core::Result<void, InstrumentError>::ok();
}

trading::core::Result<void, InstrumentError>
validate_master_structure(const InstrumentMaster& master) {
  if (master.instruments.empty()) {
    return validation_error("at least one [[instrument]] entry is required");
  }

  std::unordered_set<std::string> symbols;
  std::unordered_set<std::uint32_t> instrument_ids;
  for (const auto& instrument : master.instruments) {
    if (instrument.symbol.empty()) {
      return validation_error("instrument.symbol is required");
    }
    if (!symbols.insert(instrument.symbol).second) {
      return validation_error("duplicate symbol: " + instrument.symbol);
    }
    if (instrument.venue.empty()) {
      return validation_error("instrument.venue is required for " + instrument.symbol);
    }
    if (instrument.lot_size <= 0) {
      return validation_error("lot_size must be positive for " + instrument.symbol);
    }
    if (instrument.tick_size <= 0) {
      return validation_error("tick_size must be positive for " + instrument.symbol);
    }
    if (instrument.underlying.empty()) {
      return validation_error("underlying is required for " + instrument.symbol);
    }
    if (instrument.instrument_id.has_value()) {
      if (*instrument.instrument_id == 0) {
        return validation_error("instrument_id must be positive for " + instrument.symbol);
      }
      if (!instrument_ids.insert(*instrument.instrument_id).second) {
        return validation_error("duplicate instrument_id: " +
                                std::to_string(*instrument.instrument_id));
      }
    }
    if (instrument.option_type != OptionType::None) {
      if (!instrument.expiry_yyyy_mm_dd.has_value()) {
        return validation_error("expiry required for option " + instrument.symbol);
      }
      if (instrument.strike_ticks <= 0) {
        return validation_error("strike must be positive for option " + instrument.symbol);
      }
    }
  }

  return trading::core::Result<void, InstrumentError>::ok();
}

} // namespace trading::instruments
