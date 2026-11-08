#pragma once

#include "trading/broker/broker.hpp"
#include "trading/broker/delta/private_ws.hpp"
#include "trading/broker/delta/rest_client.hpp"
#include "trading/broker/rate_limiter.hpp"

#include <memory>

namespace trading::broker::delta {

struct DeltaAdapterConfig {
  RestClientConfig rest{};
  PrivateWsConfig private_ws{};
};

class DeltaBroker final : public IBroker {
public:
  DeltaBroker();
  explicit DeltaBroker(DeltaAdapterConfig config);

  void set_http_transport(HttpTransport* http);
  void set_websocket_transport(WebSocketTransport* ws);

  void configure_rate_limits();

  [[nodiscard]] BrokerHealth health() const override;

  bool connect() override;
  void disconnect() override;

  [[nodiscard]] SubmitResult place_order(const PlaceOrderRequest& request) override;
  [[nodiscard]] CancelResult cancel_order(const orders::ClientOrderId& client_id) override;
  [[nodiscard]] QueryResult query_order(const orders::ClientOrderId& client_id) override;

  [[nodiscard]] std::vector<PrivateEvent> poll_private_events() override;
  void tick(std::int64_t now_mono_ns) override;

  [[nodiscard]] CategoryRateLimiter& rate_limiter() noexcept { return rate_limiter_; }
  [[nodiscard]] RestClient& rest_client() noexcept { return rest_; }

private:
  [[nodiscard]] std::int64_t now_mono_ns() const noexcept;

  DeltaAdapterConfig config_;
  CategoryRateLimiter rate_limiter_{};
  RestClient rest_;
  PrivateWebSocket private_ws_;
  mutable std::int64_t last_mono_ns_{0};
};

} // namespace trading::broker::delta
