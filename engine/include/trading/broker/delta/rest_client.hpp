#pragma once

#include "trading/broker/errors.hpp"
#include "trading/broker/rate_limiter.hpp"
#include "trading/core/result.hpp"
#include "trading/orders/types.hpp"
#include "trading/broker/transport.hpp"
#include "trading/broker/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace trading::broker::delta {

struct RestClientConfig {
  std::string base_url{"https://api.delta.exchange"};
  CredentialPort credentials{};
  std::int64_t (*now_seconds)(){nullptr};
};

class RestClient {
public:
  explicit RestClient(RestClientConfig config);

  void set_http_transport(HttpTransport* transport);
  void set_rate_limiter(CategoryRateLimiter* limiter);

  [[nodiscard]] core::Result<SubmitResult, BrokerError> place_order(const PlaceOrderRequest& request,
                                                                   std::int64_t now_mono_ns);
  [[nodiscard]] core::Result<CancelResult, BrokerError> cancel_order(const orders::ClientOrderId& client_id,
                                                                     std::int64_t now_mono_ns);
  [[nodiscard]] core::Result<QueryResult, BrokerError> query_order(const orders::ClientOrderId& client_id,
                                                                 std::int64_t now_mono_ns);

private:
  [[nodiscard]] core::Result<HttpResponse, BrokerError>
  send_signed(const std::string& method, const std::string& path, const std::string& query,
              const std::string& body, journal::v1::EndpointCategory category,
              RequestPriority priority, std::int64_t now_mono_ns);

  RestClientConfig config_;
  HttpTransport* http_{nullptr};
  CategoryRateLimiter* limiter_{nullptr};
};

} // namespace trading::broker::delta
