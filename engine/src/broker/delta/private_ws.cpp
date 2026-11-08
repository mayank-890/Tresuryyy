#include "trading/broker/delta/private_ws.hpp"

#include "trading/broker/delta/signing.hpp"

#include <chrono>
#include <cmath>
#include <random>

#include <nlohmann/json.hpp>

namespace trading::broker::delta {

namespace {

[[nodiscard]] std::int64_t now_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] orders::ClientOrderId client_id_from_text(const std::string& text) {
  orders::ClientOrderId id{};
  for (std::size_t i = 0; i < text.size() && i < id.bytes.size(); ++i) {
    id.bytes[i] = text[i];
  }
  return id;
}

} // namespace

PrivateWebSocket::PrivateWebSocket(const PrivateWsConfig config) : config_(config) {}

void PrivateWebSocket::set_transport(WebSocketTransport* transport) { transport_ = transport; }

bool PrivateWebSocket::connect() {
  if (transport_ == nullptr) {
    return false;
  }
  if (!transport_->connect(config_.url)) {
    connected_ = false;
    return false;
  }
  connected_ = true;
  awaiting_reconnect_ = false;
  reconnect_count_ = 0;
  last_outbound_mono_ns_ = 0;
  last_inbound_mono_ns_ = 0;
  send_subscribe();
  events_.push_back(PrivateEvent{.kind = PrivateEventKind::Connected});
  return true;
}

void PrivateWebSocket::disconnect() {
  if (transport_ != nullptr && transport_->connected()) {
    transport_->disconnect();
  }
  connected_ = false;
  events_.push_back(PrivateEvent{.kind = PrivateEventKind::Disconnected});
}

void PrivateWebSocket::tick(const std::int64_t now_mono_ns) {
  if (transport_ == nullptr) {
    return;
  }

  if (awaiting_reconnect_) {
    if (now_mono_ns >= next_reconnect_mono_ns_) {
      awaiting_reconnect_ = false;
      static_cast<void>(connect());
    }
    return;
  }

  if (!connected_) {
    return;
  }

  while (const auto message = transport_->try_recv()) {
    last_inbound_mono_ns_ = now_mono_ns;
    handle_message(*message);
  }

  if (last_outbound_mono_ns_ == 0) {
    last_outbound_mono_ns_ = now_mono_ns;
    last_inbound_mono_ns_ = now_mono_ns;
  }

  if (now_mono_ns - last_outbound_mono_ns_ >= kPrivateWsHeartbeatIntervalNs) {
    send_heartbeat();
    last_outbound_mono_ns_ = now_mono_ns;
  }

  if (last_inbound_mono_ns_ > 0 && now_mono_ns - last_inbound_mono_ns_ >= kPrivateWsHeartbeatTimeoutNs) {
    transport_->disconnect();
    connected_ = false;
    events_.push_back(PrivateEvent{.kind = PrivateEventKind::Disconnected});
    schedule_reconnect(now_mono_ns);
  }
}

std::vector<PrivateEvent> PrivateWebSocket::drain_events() {
  std::vector<PrivateEvent> out(events_.begin(), events_.end());
  events_.clear();
  return out;
}

void PrivateWebSocket::send_subscribe() {
  ApiCredentials credentials{};
  if (config_.credentials.load == nullptr ||
      !config_.credentials.load(config_.credentials.ctx, credentials)) {
    return;
  }

  const std::string timestamp = std::to_string(now_seconds());
  const std::string signature =
      sign_request(credentials.api_secret, "GET", timestamp, "/live", "", "");

  nlohmann::json auth{};
  auth["type"] = "auth";
  auth["payload"]["api_key"] = credentials.api_key;
  auth["payload"]["signature"] = signature;
  auth["payload"]["timestamp"] = timestamp;
  static_cast<void>(transport_->send(auth.dump()));

  nlohmann::json sub{};
  sub["type"] = "subscribe";
  sub["payload"]["channels"] = nlohmann::json::array({"orders", "user_trades", "positions"});
  static_cast<void>(transport_->send(sub.dump()));
}

void PrivateWebSocket::send_heartbeat() {
  nlohmann::json ping{};
  ping["type"] = "ping";
  static_cast<void>(transport_->send(ping.dump()));
}

void PrivateWebSocket::handle_message(const std::string_view text) {
  try {
    const auto json = nlohmann::json::parse(text);
    const std::string type = json.value("type", "");
    if (type == "pong" || type == "heartbeat") {
      events_.push_back(PrivateEvent{.kind = PrivateEventKind::Heartbeat});
      return;
    }
    if (type != "order" && type != "order_update" && type != "fill" && type != "user_trade" &&
        type != "position") {
      return;
    }

    const auto& payload = json.contains("payload") ? json["payload"] : json;

    PrivateEvent event{};
    if (type == "fill" || type == "user_trade") {
      event.kind = PrivateEventKind::Fill;
      if (payload.contains("client_order_id")) {
        event.client_id = client_id_from_text(payload["client_order_id"].get<std::string>());
      }
      if (payload.contains("size")) {
        event.fill_qty_lots = payload["size"].get<std::int32_t>();
      }
      if (payload.contains("price")) {
        event.fill_price_ticks = payload["price"].get<std::int64_t>();
      }
    } else if (type == "position") {
      event.kind = PrivateEventKind::PositionUpdate;
      if (payload.contains("product_id")) {
        event.product_id = payload["product_id"].get<std::uint32_t>();
      }
      if (payload.contains("size")) {
        event.position_qty_lots = payload["size"].get<std::int32_t>();
      }
    } else {
      event.kind = PrivateEventKind::OrderUpdate;
      if (payload.contains("client_order_id")) {
        event.client_id = client_id_from_text(payload["client_order_id"].get<std::string>());
      }
      if (payload.contains("state")) {
        const std::string state = payload["state"].get<std::string>();
        if (state == "open") {
          event.order_state = orders::OrderState::Acked;
        } else if (state == "closed") {
          event.order_state = orders::OrderState::Filled;
        } else if (state == "cancelled") {
          event.order_state = orders::OrderState::Cancelled;
        }
      }
    }
    events_.push_back(event);
  } catch (...) {
    return;
  }
}

void PrivateWebSocket::schedule_reconnect(const std::int64_t now_mono_ns) {
  awaiting_reconnect_ = true;
  ++reconnect_count_;
  const std::int64_t delay = reconnect_delay_ns(reconnect_count_);
  next_reconnect_mono_ns_ = now_mono_ns + delay;
}

std::int64_t PrivateWebSocket::reconnect_delay_ns(const std::uint32_t attempt) const noexcept {
  const double base = static_cast<double>(kPrivateWsReconnectBaseNs);
  const double cap = static_cast<double>(kPrivateWsReconnectCapNs);
  const double scaled = std::min(cap, base * std::pow(2.0, static_cast<double>(attempt - 1)));
  thread_local std::mt19937_64 rng{42};
  std::uniform_real_distribution<double> jitter(0.8, 1.2);
  return static_cast<std::int64_t>(scaled * jitter(rng));
}

} // namespace trading::broker::delta
