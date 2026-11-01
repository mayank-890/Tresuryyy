#include "trading/orders/transition.hpp"

namespace trading::orders {

namespace {

using core::Result;

[[nodiscard]] bool reportable_query_state(OrderState state) {
  switch (state) {
  case OrderState::PendingNew:
  case OrderState::Acked:
  case OrderState::PartiallyFilled:
  case OrderState::Filled:
  case OrderState::PendingCancel:
  case OrderState::Cancelled:
  case OrderState::Rejected:
  case OrderState::Expired:
    return true;
  case OrderState::IntentJournaled:
  case OrderState::Unknown:
    return false;
  }
  return false;
}

[[nodiscard]] bool accepts_fill(OrderState state) {
  return state == OrderState::Acked || state == OrderState::PartiallyFilled ||
         state == OrderState::PendingCancel;
}

[[nodiscard]] Result<TransitionResult, OrderError> ok_copy(const OrderView& current) {
  TransitionResult out;
  out.state = current.state;
  out.filled_qty_lots = current.filled_qty_lots;
  out.reject_reason = current.reject_reason;
  return Result<TransitionResult, OrderError>::ok(out);
}

[[nodiscard]] Result<TransitionResult, OrderError> invalid() {
  return Result<TransitionResult, OrderError>::err(OrderError::InvalidTransition);
}

[[nodiscard]] Result<TransitionResult, OrderError> apply_fill(const OrderView& current,
                                                             const OrderEvent& event) {
  if (!accepts_fill(current.state) || current.qty_lots <= 0 || event.fill_qty_lots <= 0) {
    return invalid();
  }
  std::int32_t sum = 0;
  if (__builtin_add_overflow(current.filled_qty_lots, event.fill_qty_lots, &sum) || sum <= 0 ||
      sum > current.qty_lots) {
    return invalid();
  }
  TransitionResult out;
  out.filled_qty_lots = sum;
  out.reject_reason = current.reject_reason;
  out.state = sum == current.qty_lots ? OrderState::Filled : OrderState::PartiallyFilled;
  return Result<TransitionResult, OrderError>::ok(out);
}

[[nodiscard]] Result<TransitionResult, OrderError> apply_query_found(const OrderView& current,
                                                                     const OrderEvent& event) {
  if (current.state != OrderState::Unknown || !reportable_query_state(event.queried_state)) {
    return invalid();
  }
  TransitionResult out;
  out.state = event.queried_state;
  out.filled_qty_lots = current.filled_qty_lots;
  out.reject_reason = current.reject_reason;
  if (event.queried_state == OrderState::Rejected) {
    out.reject_reason = RejectReason::Broker;
  } else if (event.queried_state == OrderState::Filled) {
    if (current.qty_lots <= 0) {
      return invalid();
    }
    if (event.reported_filled_qty != 0 && event.reported_filled_qty != current.qty_lots) {
      return invalid();
    }
    out.filled_qty_lots = current.qty_lots;
  } else if (event.queried_state == OrderState::PartiallyFilled) {
    if (event.reported_filled_qty <= 0 || event.reported_filled_qty >= current.qty_lots) {
      return invalid();
    }
    out.filled_qty_lots = event.reported_filled_qty;
  }
  return Result<TransitionResult, OrderError>::ok(out);
}

} // namespace

core::Result<TransitionResult, OrderError> apply_transition(const OrderView& current,
                                                            const OrderEvent& event,
                                                            bool duplicate_fill) {
  if (duplicate_fill && event.kind == OrderEventKind::Fill) {
    auto copy = ok_copy(current);
    copy.value().duplicate_fill = true;
    return copy;
  }

  switch (event.kind) {
  case OrderEventKind::SentToBroker:
    if (current.state != OrderState::IntentJournaled) {
      return invalid();
    }
    {
      auto out = ok_copy(current);
      out.value().state = OrderState::PendingNew;
      return out;
    }
  case OrderEventKind::LocalReject:
    if (current.state != OrderState::IntentJournaled ||
        (event.reject_reason != RejectReason::LocalValidation &&
         event.reject_reason != RejectReason::RiskGate)) {
      return invalid();
    }
    {
      auto out = ok_copy(current);
      out.value().state = OrderState::Rejected;
      out.value().reject_reason = event.reject_reason;
      return out;
    }
  case OrderEventKind::BrokerAck:
    if (current.state != OrderState::PendingNew) {
      return invalid();
    }
    {
      auto out = ok_copy(current);
      out.value().state = OrderState::Acked;
      return out;
    }
  case OrderEventKind::BrokerDefinitiveReject:
    if (current.state != OrderState::PendingNew) {
      return invalid();
    }
    {
      auto out = ok_copy(current);
      out.value().state = OrderState::Rejected;
      out.value().reject_reason = RejectReason::Broker;
      return out;
    }
  case OrderEventKind::TimeoutOrAmbiguous:
    if (current.state != OrderState::PendingNew) {
      return invalid();
    }
    {
      auto out = ok_copy(current);
      out.value().state = OrderState::Unknown;
      return out;
    }
  case OrderEventKind::Fill:
    return apply_fill(current, event);
  case OrderEventKind::CancelRequested:
    if (current.state != OrderState::Acked && current.state != OrderState::PartiallyFilled) {
      return invalid();
    }
    {
      auto out = ok_copy(current);
      out.value().state = OrderState::PendingCancel;
      return out;
    }
  case OrderEventKind::CancelConfirmed:
    if (current.state != OrderState::PendingCancel) {
      return invalid();
    }
    {
      auto out = ok_copy(current);
      out.value().state = OrderState::Cancelled;
      return out;
    }
  case OrderEventKind::ExchangeCancel:
    if (current.state != OrderState::Acked && current.state != OrderState::PartiallyFilled) {
      return invalid();
    }
    {
      auto out = ok_copy(current);
      out.value().state = OrderState::Cancelled;
      return out;
    }
  case OrderEventKind::Expiry:
    if (current.state != OrderState::Acked && current.state != OrderState::PartiallyFilled) {
      return invalid();
    }
    {
      auto out = ok_copy(current);
      out.value().state = OrderState::Expired;
      return out;
    }
  case OrderEventKind::QueryFound:
    return apply_query_found(current, event);
  case OrderEventKind::QueryNotFoundAfterRetry:
    if (current.state != OrderState::Unknown) {
      return invalid();
    }
    {
      auto out = ok_copy(current);
      out.value().state = OrderState::Rejected;
      out.value().reject_reason = RejectReason::NotFoundAfterQuery;
      return out;
    }
  }
  return invalid();
}

} // namespace trading::orders
