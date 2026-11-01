#include "trading/orders/order_book.hpp"

#include "trading/orders/transition.hpp"

#include <cstring>
#include <utility>

namespace trading::orders {

namespace {

constexpr char kInvalidTransitionMessage[] = "invalid transition";

[[nodiscard]] bool fill_seen(const StoredOrder& order, std::uint64_t fill_id) {
  if (fill_id == 0) {
    return false;
  }
  for (const std::uint64_t seen : order.fill_ids) {
    if (seen == fill_id) {
      return true;
    }
  }
  return false;
}

} // namespace

OrderBook::OrderBook(IdSource& ids, JournalPort journal, SendPort send)
    : ids_(ids), journal_(journal), send_(send) {}

bool OrderBook::has_scope(const std::vector<ScopeKey>& keys, std::uint32_t strategy_id,
                          std::uint32_t instrument_id) const {
  for (const ScopeKey& key : keys) {
    if (key.strategy_id == strategy_id && key.instrument_id == instrument_id) {
      return true;
    }
  }
  return false;
}

bool OrderBook::scope_halted(std::uint32_t strategy_id, std::uint32_t instrument_id) const {
  return has_scope(halted_, strategy_id, instrument_id);
}

bool OrderBook::scope_blocked(std::uint32_t strategy_id, std::uint32_t instrument_id) const {
  return has_scope(blocked_, strategy_id, instrument_id);
}

const StoredOrder* OrderBook::find_const(const ClientOrderId& id) const {
  for (const StoredOrder& order : orders_) {
    if (order.client_id == id) {
      return &order;
    }
  }
  return nullptr;
}

StoredOrder* OrderBook::find_mut(const ClientOrderId& id) {
  for (StoredOrder& order : orders_) {
    if (order.client_id == id) {
      return &order;
    }
  }
  return nullptr;
}

const StoredOrder* OrderBook::find(const ClientOrderId& id) const { return find_const(id); }

void OrderBook::log_reject(const StoredOrder& order, const OrderEvent& event) {
  TransitionLog entry;
  entry.client_id = order.client_id;
  entry.from = order.state;
  entry.event = event.kind;
  entry.rejected = true;
  static_assert(sizeof(kInvalidTransitionMessage) <= sizeof(entry.message));
  std::memcpy(entry.message, kInvalidTransitionMessage, sizeof(kInvalidTransitionMessage));
  logs_.push_back(entry);
}

void OrderBook::raise_defect(const StoredOrder& order, const OrderEvent& event) {
  DefectRecord defect;
  defect.client_id = order.client_id;
  defect.from = order.state;
  defect.event = event.kind;
  defects_.push_back(defect);
}

void OrderBook::halt(std::uint32_t strategy_id, std::uint32_t instrument_id) {
  if (!scope_halted(strategy_id, instrument_id)) {
    halted_.push_back(ScopeKey{strategy_id, instrument_id});
  }
}

void OrderBook::block(std::uint32_t strategy_id, std::uint32_t instrument_id) {
  if (!scope_blocked(strategy_id, instrument_id)) {
    blocked_.push_back(ScopeKey{strategy_id, instrument_id});
  }
}

void OrderBook::clear_block_if_idle(std::uint32_t strategy_id, std::uint32_t instrument_id) {
  for (const StoredOrder& order : orders_) {
    if (order.request.strategy_id == strategy_id && order.request.instrument_id == instrument_id &&
        order.state == OrderState::Unknown) {
      return;
    }
  }
  std::erase_if(blocked_, [&](const ScopeKey& key) {
    return key.strategy_id == strategy_id && key.instrument_id == instrument_id;
  });
}

void OrderBook::raise_al04(const ClientOrderId& id) {
  AlertRecord alert;
  alert.code = kAlertAl04;
  alert.client_id = id;
  alerts_.push_back(alert);
}

core::Result<ClientOrderId, OrderError> OrderBook::open_intent(const IntentRequest& request) {
  if (request.qty_lots <= 0) {
    return core::Result<ClientOrderId, OrderError>::err(OrderError::InvalidQuantity);
  }
  if (scope_halted(request.strategy_id, request.instrument_id)) {
    return core::Result<ClientOrderId, OrderError>::err(OrderError::ScopeHalted);
  }
  if (scope_blocked(request.strategy_id, request.instrument_id)) {
    return core::Result<ClientOrderId, OrderError>::err(OrderError::ScopeBlocked);
  }
  auto issued = ids_.next();
  if (!issued) {
    return core::Result<ClientOrderId, OrderError>::err(issued.error());
  }
  if (find_const(issued.value()) != nullptr) {
    return core::Result<ClientOrderId, OrderError>::err(OrderError::DuplicateIntent);
  }
  if (journal_.record_intent == nullptr ||
      !journal_.record_intent(journal_.ctx, request, issued.value())) {
    return core::Result<ClientOrderId, OrderError>::err(OrderError::JournalFailed);
  }
  StoredOrder order;
  order.client_id = issued.value();
  order.request = request;
  order.state = OrderState::IntentJournaled;
  order.intent_durable = true;
  orders_.push_back(std::move(order));
  return core::Result<ClientOrderId, OrderError>::ok(issued.value());
}

core::Result<OrderState, OrderError> OrderBook::commit(StoredOrder& order, const OrderEvent& event) {
  // A journaled intent is still a new order until it is sent. UNKNOWN blocks
  // that submission. Any other SentToBroker is an invalid transition.
  if (event.kind == OrderEventKind::SentToBroker && order.state == OrderState::IntentJournaled &&
      scope_blocked(order.request.strategy_id, order.request.instrument_id)) {
    return core::Result<OrderState, OrderError>::err(OrderError::ScopeBlocked);
  }
  const bool duplicate = event.kind == OrderEventKind::Fill && fill_seen(order, event.fill_id);
  const OrderView view{order.state, order.request.qty_lots, order.filled_qty_lots,
                       order.reject_reason};
  auto outcome = apply_transition(view, event, duplicate);
  if (!outcome) {
    log_reject(order, event);
    raise_defect(order, event);
    halt(order.request.strategy_id, order.request.instrument_id);
    return core::Result<OrderState, OrderError>::err(outcome.error());
  }
  if (outcome.value().duplicate_fill) {
    ++order.duplicate_fill_count;
    return core::Result<OrderState, OrderError>::ok(order.state);
  }
  const OrderState from = order.state;
  const OrderState to = outcome.value().state;
  if (journal_.record_transition == nullptr ||
      !journal_.record_transition(journal_.ctx, order.client_id, from, to,
                                  static_cast<std::uint16_t>(outcome.value().reject_reason))) {
    return core::Result<OrderState, OrderError>::err(OrderError::JournalFailed);
  }
  order.state = to;
  order.filled_qty_lots = outcome.value().filled_qty_lots;
  order.reject_reason = outcome.value().reject_reason;
  if (event.kind == OrderEventKind::Fill && event.fill_id != 0) {
    order.fill_ids.push_back(event.fill_id);
  }
  if (to == OrderState::Unknown) {
    block(order.request.strategy_id, order.request.instrument_id);
  }
  if (from == OrderState::Unknown && to != OrderState::Unknown) {
    clear_block_if_idle(order.request.strategy_id, order.request.instrument_id);
  }
  if (to == OrderState::PendingCancel && send_.send_cancel != nullptr) {
    send_.send_cancel(send_.ctx, order.client_id);
  }
  return core::Result<OrderState, OrderError>::ok(to);
}

core::Result<OrderState, OrderError> OrderBook::apply(const ClientOrderId& id,
                                                      const OrderEvent& event) {
  StoredOrder* order = find_mut(id);
  if (order == nullptr) {
    return core::Result<OrderState, OrderError>::err(OrderError::UnknownClientId);
  }
  if (scope_halted(order->request.strategy_id, order->request.instrument_id)) {
    return core::Result<OrderState, OrderError>::err(OrderError::ScopeHalted);
  }
  // The gateway is invoked only from send(), after the intent and the sent
  // transition are both durable.
  if (event.kind == OrderEventKind::SentToBroker && order->state == OrderState::IntentJournaled) {
    return send(id);
  }
  return commit(*order, event);
}

core::Result<OrderState, OrderError> OrderBook::send(const ClientOrderId& id) {
  StoredOrder* order = find_mut(id);
  if (order == nullptr) {
    return core::Result<OrderState, OrderError>::err(OrderError::UnknownClientId);
  }
  if (!order->intent_durable) {
    return core::Result<OrderState, OrderError>::err(OrderError::IntentNotDurable);
  }
  if (scope_halted(order->request.strategy_id, order->request.instrument_id)) {
    return core::Result<OrderState, OrderError>::err(OrderError::ScopeHalted);
  }
  OrderEvent event;
  event.kind = OrderEventKind::SentToBroker;
  auto sent = commit(*order, event);
  if (!sent) {
    return sent;
  }
  // Re-find: commit does not reallocate, but the send must observe the
  // durable id that was journaled with the intent.
  const StoredOrder* durable = find_const(id);
  if (durable == nullptr || !durable->intent_durable || durable->state != OrderState::PendingNew) {
    return core::Result<OrderState, OrderError>::err(OrderError::IntentNotDurable);
  }
  if (send_.send_new != nullptr) {
    send_.send_new(send_.ctx, durable->request, durable->client_id);
  }
  return sent;
}

core::Result<OrderState, OrderError> OrderBook::resolve_unknown(const ClientOrderId& id,
                                                                LookupPort lookup, DelayPort delay) {
  StoredOrder* order = find_mut(id);
  if (order == nullptr) {
    return core::Result<OrderState, OrderError>::err(OrderError::UnknownClientId);
  }
  if (order->state != OrderState::Unknown) {
    return core::Result<OrderState, OrderError>::err(OrderError::NotUnknown);
  }
  if (order->unknown_resend_attempted) {
    return core::Result<OrderState, OrderError>::err(OrderError::AwaitingOperator);
  }

  const ClientOrderId client_id = order->client_id;
  const IntentRequest request = order->request;
  block(request.strategy_id, request.instrument_id);

  const std::uint32_t delays[2] = {kUnknownQueryDelayBeforeSecondMs, kUnknownQueryDelayBeforeThirdMs};
  for (int attempt = 0; attempt < kUnknownQueryAttempts; ++attempt) {
    if (attempt > 0 && delay.delay_ms != nullptr) {
      delay.delay_ms(delay.ctx, delays[attempt - 1]);
    }
    QueryResponse response;
    if (lookup.query_by_client_id != nullptr) {
      response = lookup.query_by_client_id(lookup.ctx, client_id.text());
    }
    if (response.status != QueryResponse::Status::Found) {
      continue;
    }
    OrderEvent event;
    event.kind = OrderEventKind::QueryFound;
    event.queried_state = response.state;
    event.reported_filled_qty = response.reported_filled_qty;
    StoredOrder* current = find_mut(client_id);
    if (current == nullptr) {
      return core::Result<OrderState, OrderError>::err(OrderError::UnknownClientId);
    }
    const OrderView view{current->state, current->request.qty_lots, current->filled_qty_lots,
                         current->reject_reason};
    // A query payload that is not a legal reported state is not a defect.
    // Keep querying. Do not halt the scope.
    if (!apply_transition(view, event, false)) {
      continue;
    }
    return commit(*current, event);
  }

  StoredOrder* current = find_mut(client_id);
  if (current == nullptr) {
    return core::Result<OrderState, OrderError>::err(OrderError::UnknownClientId);
  }
  // Once, with the original client id. A later call will not resend.
  current->unknown_resend_attempted = true;
  ResendResponse resend;
  if (lookup.resend_same_client_id != nullptr) {
    resend = lookup.resend_same_client_id(lookup.ctx, request, client_id);
  }
  if (resend.status == ResendResponse::Status::Resolved) {
    OrderEvent event;
    event.kind = OrderEventKind::QueryFound;
    event.queried_state = resend.state;
    event.reported_filled_qty = resend.reported_filled_qty;
    current = find_mut(client_id);
    if (current == nullptr) {
      return core::Result<OrderState, OrderError>::err(OrderError::UnknownClientId);
    }
    const OrderView view{current->state, current->request.qty_lots, current->filled_qty_lots,
                         current->reject_reason};
    if (apply_transition(view, event, false)) {
      return commit(*current, event);
    }
  } else if (resend.status == ResendResponse::Status::NotFound) {
    OrderEvent event;
    event.kind = OrderEventKind::QueryNotFoundAfterRetry;
    current = find_mut(client_id);
    if (current == nullptr) {
      return core::Result<OrderState, OrderError>::err(OrderError::UnknownClientId);
    }
    return commit(*current, event);
  }

  raise_al04(client_id);
  return core::Result<OrderState, OrderError>::ok(OrderState::Unknown);
}

} // namespace trading::orders
