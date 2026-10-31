#include "strategies/flat_echo/strategy.hpp"

#include <cstring>

#include "active_strategies.hpp"

TRADING_REGISTER_STRATEGY(trading::strategy::flat_echo::FlatEcho);

namespace trading::strategy::flat_echo {

std::size_t FlatEcho::decide(const MarketSnapshot& market, const ParamView& params,
                             std::span<OrderIntent> out) const {
  if (params.bytes.size() != sizeof(FlatParams) || params.bytes.data() == nullptr) {
    return 0;
  }
  FlatParams local{};
  std::memcpy(&local, params.bytes.data(), sizeof(local));
  if (local.version != params.version) {
    return 0;
  }
  const auto lots = size_lots(local);
  if (lots <= 0 || out.empty()) {
    return 0;
  }
  if (local.side != Side::Buy && local.side != Side::Sell) {
    return 0;
  }

  const Quote* quote = nullptr;
  for (const Quote& candidate : market.quotes) {
    if (candidate.instrument_id == local.instrument_id) {
      quote = &candidate;
      break;
    }
  }
  if (quote == nullptr) {
    return 0;
  }

  OrderIntent intent{};
  intent.strategy_id = strategy_id_;
  intent.instrument_id = local.instrument_id;
  intent.side = local.side;
  intent.qty_lots = lots;
  intent.price_ticks = local.side == Side::Buy ? quote->ask_ticks : quote->bid_ticks;
  intent.time_in_force = local.time_in_force;
  intent.param_version = local.version;
  intent.snapshot_sequence = market.sequence;
  out[0] = intent;
  return 1;
}

} // namespace trading::strategy::flat_echo
