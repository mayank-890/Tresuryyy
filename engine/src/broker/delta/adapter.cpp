#include "trading/broker/delta/adapter.hpp"

namespace trading::broker::delta {

namespace {

[[nodiscard]] SubmitResult ambiguous_submit(const BrokerError error) {
  SubmitResult result{};
  result.disposition = SubmitDisposition::Ambiguous;
  switch (error) {
  case BrokerError::RateLimited:
    result.reason = "rate_limited";
    break;
  case BrokerError::CredentialsMissing:
    result.reason = "credentials_missing";
    break;
  case BrokerError::TransportFailed:
    result.reason = "transport_failed";
    break;
  default:
    result.reason = "broker_error";
    break;
  }
  return result;
}

[[nodiscard]] CancelResult ambiguous_cancel(const BrokerError error) {
  CancelResult result{};
  result.disposition = CancelDisposition::Ambiguous;
  result.reason = error == BrokerError::RateLimited ? "rate_limited" : "broker_error";
  return result;
}

[[nodiscard]] QueryResult error_query(const BrokerError error) {
  QueryResult result{};
  result.disposition = QueryDisposition::Error;
  result.reason = error == BrokerError::RateLimited ? "rate_limited" : "broker_error";
  return result;
}

} // namespace

DeltaBroker::DeltaBroker() : DeltaBroker(DeltaAdapterConfig{}) {}

DeltaBroker::DeltaBroker(const DeltaAdapterConfig config)
    : config_(config), rest_(config.rest), private_ws_(config.private_ws) {
  rest_.set_rate_limiter(&rate_limiter_);
  configure_rate_limits();
}

void DeltaBroker::set_http_transport(HttpTransport* http) {
  rest_.set_http_transport(http);
}

void DeltaBroker::set_websocket_transport(WebSocketTransport* ws) {
  private_ws_.set_transport(ws);
}

void DeltaBroker::configure_rate_limits() {
  rate_limiter_.configure(journal::v1::EndpointCategory::OrderEntry, 10.0, 5.0);
  rate_limiter_.configure(journal::v1::EndpointCategory::OrderQuery, 20.0, 10.0);
  rate_limiter_.configure(journal::v1::EndpointCategory::Cancel, 15.0, 15.0);
}

BrokerHealth DeltaBroker::health() const {
  BrokerHealth health{};
  health.rest_ok = true;
  health.private_ws_ok = private_ws_.connected();
  if (health.rest_ok && health.private_ws_ok) {
    health.state = BrokerHealthState::Connected;
  } else if (health.rest_ok || health.private_ws_ok) {
    health.state = BrokerHealthState::Degraded;
  } else if (private_ws_.reconnect_count() > 0) {
    health.state = BrokerHealthState::Connecting;
  } else {
    health.state = BrokerHealthState::Disconnected;
  }
  return health;
}

bool DeltaBroker::connect() {
  const bool ws_ok = private_ws_.connect();
  return ws_ok;
}

void DeltaBroker::disconnect() {
  private_ws_.disconnect();
}

SubmitResult DeltaBroker::place_order(const PlaceOrderRequest& request) {
  const auto result = rest_.place_order(request, now_mono_ns());
  if (!result.is_ok()) {
    return ambiguous_submit(result.error());
  }
  return result.value();
}

CancelResult DeltaBroker::cancel_order(const orders::ClientOrderId& client_id) {
  const auto result = rest_.cancel_order(client_id, now_mono_ns());
  if (!result.is_ok()) {
    return ambiguous_cancel(result.error());
  }
  return result.value();
}

QueryResult DeltaBroker::query_order(const orders::ClientOrderId& client_id) {
  const auto result = rest_.query_order(client_id, now_mono_ns());
  if (!result.is_ok()) {
    return error_query(result.error());
  }
  return result.value();
}

std::vector<PrivateEvent> DeltaBroker::poll_private_events() { return private_ws_.drain_events(); }

void DeltaBroker::tick(const std::int64_t now_mono_ns) {
  last_mono_ns_ = now_mono_ns;
  private_ws_.tick(now_mono_ns);
}

std::int64_t DeltaBroker::now_mono_ns() const noexcept {
  if (last_mono_ns_ > 0) {
    return last_mono_ns_;
  }
  return 1;
}

} // namespace trading::broker::delta
