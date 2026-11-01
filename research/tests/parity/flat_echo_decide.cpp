#include "strategies/flat_echo/strategy.hpp"

#include <array>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kMaxQuotes = 8;
constexpr std::size_t kMaxIntents = 4;

} // namespace

int main() {
  using trading::strategy::MarketSnapshot;
  using trading::strategy::OrderIntent;
  using trading::strategy::Quote;
  using trading::strategy::Side;
  using trading::strategy::TimeInForce;
  using trading::strategy::flat_echo::FlatEcho;
  using trading::strategy::flat_echo::FlatParams;
  using trading::strategy::param_view;

  std::string tag;
  while (std::cin >> tag) {
    if (tag != "DECIDE") {
      return 2;
    }
    std::uint32_t strategy_id = 0;
    std::uint64_t version = 0;
    std::uint32_t instrument_id = 0;
    std::int32_t qty_lots = 0;
    unsigned side_raw = 0;
    unsigned tif_raw = 0;
    std::uint64_t sequence = 0;
    std::size_t quote_count = 0;
    if (!(std::cin >> strategy_id >> version >> instrument_id >> qty_lots >> side_raw >> tif_raw >>
          sequence >> quote_count)) {
      return 2;
    }
    if (quote_count > kMaxQuotes || (side_raw > 2) || tif_raw < 1 || tif_raw > 3) {
      return 2;
    }
    std::vector<Quote> quotes(quote_count);
    for (std::size_t i = 0; i < quote_count; ++i) {
      Quote quote{};
      if (!(std::cin >> quote.instrument_id >> quote.bid_ticks >> quote.ask_ticks >>
            quote.bid_size_lots >> quote.ask_size_lots >> quote.exchange_time_ns)) {
        return 2;
      }
      quotes[i] = quote;
    }

    FlatParams params{};
    params.version = version;
    params.instrument_id = instrument_id;
    params.qty_lots = qty_lots;
    params.side = static_cast<Side>(side_raw);
    params.time_in_force = static_cast<TimeInForce>(tif_raw);

    const FlatEcho strategy{strategy_id};
    const MarketSnapshot market{sequence, std::span<const Quote>{quotes}};
    std::array<OrderIntent, kMaxIntents> out{};
    const auto written = strategy.decide(market, param_view(params), out);
    std::cout << "COUNT " << written << '\n';
    for (std::size_t i = 0; i < written && i < out.size(); ++i) {
      const OrderIntent& intent = out[i];
      std::cout << intent.strategy_id << ' ' << intent.instrument_id << ' '
                << static_cast<unsigned>(intent.side) << ' ' << intent.qty_lots << ' '
                << intent.price_ticks << ' ' << static_cast<unsigned>(intent.time_in_force) << ' '
                << intent.param_version << ' ' << intent.snapshot_sequence << '\n';
    }
  }
  return 0;
}
