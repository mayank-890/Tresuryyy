#include "trading/broker/mock/adapter.hpp"

#include <algorithm>

namespace trading::broker::mock {

namespace {

[[nodiscard]] std::string client_key(const orders::ClientOrderId& id) {
  return std::string(id.text());
}

} // namespace

MockExchangeBroker::MockExchangeBroker() = default;

MockExchangeBroker::MockExchangeBroker(const MockScenario scenario) : scenario_(scenario) {
  if (scenario_.seed != 0) {
    next_auto_broker_id_ = scenario_.seed;
  }
}

BrokerHealth MockExchangeBroker::health() const {
  BrokerHealth health{};
  health.rest_ok = link_up_ && session_connected_;
  health.private_ws_ok = link_up_ && session_connected_;
  if (!session_connected_) {
    health.state = BrokerHealthState::Disconnected;
  } else if (!link_up_) {
    health.state = BrokerHealthState::Degraded;
  } else {
    health.state = BrokerHealthState::Connected;
  }
  return health;
}

bool MockExchangeBroker::connect() {
  if (!scenario_.connect_succeed) {
    session_connected_ = false;
    link_up_ = false;
    return false;
  }
  session_connected_ = true;
  link_up_ = true;
  pending_events_.push_back(PrivateEvent{.kind = PrivateEventKind::Connected});
  return true;
}

void MockExchangeBroker::disconnect() {
  session_connected_ = false;
  link_up_ = false;
  pending_events_.push_back(PrivateEvent{.kind = PrivateEventKind::Disconnected});
}

const PlaceStep* MockExchangeBroker::next_place_step() noexcept {
  if (place_step_index_ >= scenario_.place_steps.size()) {
    return nullptr;
  }
  return &scenario_.place_steps[place_step_index_++];
}

void MockExchangeBroker::schedule_fills(const orders::ClientOrderId& client_id,
                                         const PlaceOrderRequest& request, const PlaceStep& step,
                                         const std::int64_t place_mono_ns) {
  std::int32_t remaining = request.size_lots;
  for (const ScheduledFill& fill : step.fills) {
    if (fill.qty_lots <= 0) {
      continue;
    }
    const std::int32_t qty = std::min(remaining, fill.qty_lots);
    if (qty <= 0) {
      break;
    }
    remaining -= qty;
    const orders::OrderState state_after =
        remaining == 0 ? orders::OrderState::Filled : orders::OrderState::PartiallyFilled;
    const std::int64_t fire_at = place_mono_ns + fill.delay_from_place_ns;

    PendingEvent fill_event{};
    fill_event.fire_at_mono_ns = fire_at;
    fill_event.client_id = client_id;
    fill_event.fill_qty_lots = qty;
    fill_event.event.kind = PrivateEventKind::Fill;
    fill_event.event.client_id = client_id;
    fill_event.event.fill_qty_lots = qty;
    fill_event.event.fill_price_ticks = fill.price_ticks;
    fill_event.event.order_state = state_after;
    scheduled_.push_back(fill_event);

    PendingEvent update_event{};
    update_event.fire_at_mono_ns = fire_at;
    update_event.client_id = client_id;
    update_event.event.kind = PrivateEventKind::OrderUpdate;
    update_event.event.client_id = client_id;
    update_event.event.order_state = state_after;
    scheduled_.push_back(update_event);
  }
}

SubmitResult MockExchangeBroker::place_order(const PlaceOrderRequest& request) {
  ++place_calls_;
  if (last_mono_ns_ <= 0) {
    last_mono_ns_ = 1;
  }

  if (!session_connected_ || !link_up_) {
    SubmitResult result{};
    result.disposition = SubmitDisposition::Ambiguous;
    result.reason = session_connected_ ? "disconnected" : "not_connected";
    return result;
  }

  const PlaceStep* step = next_place_step();
  const PlaceResponseKind kind =
      step == nullptr ? PlaceResponseKind::Ack : step->response;

  SubmitResult result{};
  switch (kind) {
  case PlaceResponseKind::Reject:
    result.disposition = SubmitDisposition::Rejected;
    result.reason = step != nullptr && !step->reason.empty() ? step->reason : "mock_reject";
    return result;
  case PlaceResponseKind::Timeout:
    result.disposition = SubmitDisposition::Ambiguous;
    result.reason = "timeout";
    return result;
  case PlaceResponseKind::RateLimited:
    result.disposition = SubmitDisposition::Ambiguous;
    result.reason = "rate_limited";
    return result;
  case PlaceResponseKind::Ambiguous:
    result.disposition = SubmitDisposition::Ambiguous;
    result.reason = step != nullptr && !step->reason.empty() ? step->reason : "ambiguous";
    return result;
  case PlaceResponseKind::Ack:
    break;
  }

  std::uint64_t broker_id = step != nullptr && step->broker_order_id != 0 ? step->broker_order_id
                                                                           : next_auto_broker_id_++;
  StoredMockOrder stored{};
  stored.request = request;
  stored.broker_order_id = broker_id;
  stored.state = orders::OrderState::Acked;
  orders_[client_key(request.client_id)] = stored;

  if (step != nullptr && !step->fills.empty()) {
    schedule_fills(request.client_id, request, *step, last_mono_ns_);
  } else {
    pending_events_.push_back(PrivateEvent{.kind = PrivateEventKind::OrderUpdate,
                                            .client_id = request.client_id,
                                            .order_state = orders::OrderState::Acked});
  }

  result.disposition = SubmitDisposition::Acknowledged;
  result.broker_order_id = broker_id;
  return result;
}

CancelResult MockExchangeBroker::cancel_order(const orders::ClientOrderId& client_id) {
  if (!session_connected_ || !link_up_) {
    CancelResult result{};
    result.disposition = CancelDisposition::Ambiguous;
    result.reason = "disconnected";
    return result;
  }

  StoredMockOrder* order = find_order(client_id);
  if (order == nullptr) {
    CancelResult result{};
    result.disposition = CancelDisposition::Rejected;
    result.reason = "not_found";
    return result;
  }

  order->cancelled = true;
  order->state = orders::OrderState::Cancelled;
  pending_events_.push_back(PrivateEvent{.kind = PrivateEventKind::OrderUpdate,
                                          .client_id = client_id,
                                          .order_state = orders::OrderState::Cancelled});
  CancelResult result{};
  result.disposition = CancelDisposition::Confirmed;
  return result;
}

QueryResult MockExchangeBroker::query_order(const orders::ClientOrderId& client_id) {
  if (!session_connected_) {
    QueryResult result{};
    result.disposition = QueryDisposition::Error;
    result.reason = "not_connected";
    return result;
  }

  const StoredMockOrder* order = find_order(client_id);
  if (order == nullptr) {
    QueryResult result{};
    result.disposition = QueryDisposition::NotFound;
    return result;
  }

  QueryResult result{};
  result.disposition = QueryDisposition::Found;
  result.order.client_id = client_id;
  result.order.broker_order_id = order->broker_order_id;
  result.order.qty_lots = order->request.size_lots;
  result.order.filled_qty_lots = order->filled_qty_lots;
  result.order.state = order->state;
  return result;
}

std::vector<PrivateEvent> MockExchangeBroker::poll_private_events() {
  std::vector<PrivateEvent> out(pending_events_.begin(), pending_events_.end());
  pending_events_.clear();
  return out;
}

MockExchangeBroker::StoredMockOrder* MockExchangeBroker::find_order(
    const orders::ClientOrderId& client_id) noexcept {
  const auto it = orders_.find(client_key(client_id));
  if (it == orders_.end()) {
    return nullptr;
  }
  return &it->second;
}

void MockExchangeBroker::apply_timeline(const std::int64_t now_mono_ns) {
  while (timeline_index_ < scenario_.timeline.size() &&
         scenario_.timeline[timeline_index_].at_mono_ns <= now_mono_ns) {
    const TimelineAction& action = scenario_.timeline[timeline_index_++];
    if (action.kind == TimelineActionKind::Disconnect) {
      link_up_ = false;
      pending_events_.push_back(PrivateEvent{.kind = PrivateEventKind::Disconnected});
    } else {
      link_up_ = true;
      pending_events_.push_back(PrivateEvent{.kind = PrivateEventKind::Connected});
    }
  }
}

void MockExchangeBroker::fire_due_events(const std::int64_t now_mono_ns) {
  std::vector<PendingEvent> keep;
  keep.reserve(scheduled_.size());
  for (PendingEvent& pending : scheduled_) {
    if (pending.fire_at_mono_ns > now_mono_ns) {
      keep.push_back(std::move(pending));
      continue;
    }
    StoredMockOrder* order = find_order(pending.client_id);
    if (order != nullptr) {
      if (pending.event.kind == PrivateEventKind::Fill) {
        order->filled_qty_lots += pending.fill_qty_lots;
      }
      order->state = pending.event.order_state;
    }
    pending_events_.push_back(pending.event);
  }
  scheduled_ = std::move(keep);
}

void MockExchangeBroker::tick(const std::int64_t now_mono_ns) {
  last_mono_ns_ = now_mono_ns;
  apply_timeline(now_mono_ns);
  fire_due_events(now_mono_ns);
}

} // namespace trading::broker::mock
