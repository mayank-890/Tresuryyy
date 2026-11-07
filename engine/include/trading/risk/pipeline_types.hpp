#pragma once

#include "trading/risk/gate.hpp"
#include "trading/strategy/intent.hpp"

#include <array>
#include <cstdint>

namespace trading::risk {

// Ring C payload: T3 RiskGate → T4 Execution (approved only).
struct ApprovedIngress {
  bool warmup{false};
  strategy::OrderIntent intent{};
  std::array<char, kClientIdBytes> client_id{};
  ApprovalToken token{};
};

// T7 journal ring entry. Copied off the T3 hot path via try_push.
struct JournalIngress {
  DecisionRecord record{};
};

} // namespace trading::risk
