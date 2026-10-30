#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "trading/journal/generated/records.hpp"

namespace trading::marketdata {

struct InstrumentSpec {
  std::uint32_t instrument_id{};
  std::int64_t tick_size{};
  std::int32_t lot_size{1};
  std::string symbol;
};

struct NormalizedQuote {
  journal::v1::MarketQuotePayload payload{};
};

struct NormalizedTrade {
  journal::v1::MarketTradePayload payload{};
};

[[nodiscard]] std::int64_t price_string_to_ticks(std::string_view price, std::int64_t tick_size);

[[nodiscard]] std::optional<NormalizedQuote> parse_delta_public_message(
    std::string_view json, const InstrumentSpec& instrument);

} // namespace trading::marketdata
