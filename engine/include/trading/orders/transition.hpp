#pragma once

#include "trading/core/result.hpp"
#include "trading/orders/types.hpp"

namespace trading::orders {

struct TransitionResult {
  OrderState state{OrderState::IntentJournaled};
  std::int32_t filled_qty_lots{0};
  RejectReason reject_reason{RejectReason::None};
  bool duplicate_fill{false};
};

// DESIGN §5.2. `duplicate_fill` is set by the caller when `event.fill_id`
// was already applied. A duplicate does not move the order and is not an
// invalid transition.
[[nodiscard]] core::Result<TransitionResult, OrderError>
apply_transition(const OrderView& current, const OrderEvent& event, bool duplicate_fill);

} // namespace trading::orders
