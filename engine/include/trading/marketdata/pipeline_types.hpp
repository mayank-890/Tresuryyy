#pragma once

#include "trading/strategy/market_snapshot.hpp"

#include <cstdint>

namespace trading::marketdata {

// Ring A payload: T1 MarketData → T2 Strategy.
struct QuoteIngress {
  bool warmup{false};
  std::uint64_t sequence{0};
  strategy::Quote quote{};
};

} // namespace trading::marketdata
