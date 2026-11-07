#pragma once

#include "trading/strategy/intent.hpp"

namespace trading::strategy {

// Ring B payload: T2 Strategy → T3 RiskGate.
struct IntentIngress {
  bool warmup{false};
  OrderIntent intent{};
};

} // namespace trading::strategy
