#include "trading/marketdata/delta_normalize.hpp"

#include <cmath>
#include <nlohmann/json.hpp>

namespace trading::marketdata {

namespace {

[[nodiscard]] std::optional<double> parse_decimal(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  try {
    return std::stod(std::string(text));
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] std::int32_t parse_lots(std::string_view text, const std::int32_t lot_size) {
  const auto value = parse_decimal(text);
  if (!value.has_value()) {
    return 0;
  }
  if (lot_size <= 0) {
    return 0;
  }
  return static_cast<std::int32_t>(std::llround(*value / static_cast<double>(lot_size)));
}

} // namespace

std::int64_t price_string_to_ticks(const std::string_view price, const std::int64_t tick_size) {
  if (tick_size <= 0) {
    return 0;
  }
  const auto value = parse_decimal(price);
  if (!value.has_value()) {
    return 0;
  }
  return static_cast<std::int64_t>(std::llround(*value / static_cast<double>(tick_size)));
}

std::optional<NormalizedQuote> parse_delta_public_message(const std::string_view json,
                                                          const InstrumentSpec& instrument) {
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(json);
  } catch (...) {
    return std::nullopt;
  }

  const std::string msg_type = doc.value("type", std::string{});
  if (msg_type != "v2_ticker" && msg_type != "quote") {
    return std::nullopt;
  }

  if (!doc.contains("symbol") ||
      doc["symbol"].get<std::string>() != instrument.symbol) {
    return std::nullopt;
  }

  std::string bid;
  std::string ask;
  std::string bid_size;
  std::string ask_size;
  std::uint64_t exchange_time_ns = 0;

  if (msg_type == "quote") {
    bid = doc.value("bid", std::string{});
    ask = doc.value("ask", std::string{});
    bid_size = doc.value("bid_size", std::string{"0"});
    ask_size = doc.value("ask_size", std::string{"0"});
    if (doc.contains("timestamp_us")) {
      exchange_time_ns = doc["timestamp_us"].get<std::uint64_t>() * 1000ULL;
    }
  } else {
    const auto& quotes = doc["quotes"];
    bid = quotes.value("best_bid", std::string{});
    ask = quotes.value("best_ask", std::string{});
    bid_size = quotes.value("bid_size", std::string{"0"});
    ask_size = quotes.value("ask_size", std::string{"0"});
    if (doc.contains("timestamp")) {
      exchange_time_ns = doc["timestamp"].get<std::uint64_t>() * 1000ULL;
    }
  }

  NormalizedQuote out{};
  out.payload.instrument_id = instrument.instrument_id;
  out.payload.bid_ticks = price_string_to_ticks(bid, instrument.tick_size);
  out.payload.ask_ticks = price_string_to_ticks(ask, instrument.tick_size);
  out.payload.bid_size_lots = parse_lots(bid_size, instrument.lot_size);
  out.payload.ask_size_lots = parse_lots(ask_size, instrument.lot_size);
  out.payload.exchange_time_ns = exchange_time_ns;
  return out;
}

} // namespace trading::marketdata
