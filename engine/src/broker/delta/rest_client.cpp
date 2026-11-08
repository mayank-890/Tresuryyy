#include "trading/broker/delta/rest_client.hpp"

#include "trading/broker/delta/signing.hpp"

#include <chrono>
#include <sstream>

#include <nlohmann/json.hpp>

namespace trading::broker::delta {

namespace {

[[nodiscard]] std::int64_t default_now_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string client_id_text(const orders::ClientOrderId& id) {
  return std::string(id.text());
}

[[nodiscard]] SubmitResult parse_place_response(const HttpResponse& response) {
  SubmitResult result{};
  if (response.status >= 500) {
    result.disposition = SubmitDisposition::Ambiguous;
    result.reason = "server_error";
    return result;
  }
  if (response.status == 429) {
    result.disposition = SubmitDisposition::Ambiguous;
    result.reason = "rate_limited";
    return result;
  }
  try {
    const auto json = nlohmann::json::parse(response.body);
    if (response.status >= 400 || json.value("success", true) == false) {
      result.disposition = SubmitDisposition::Rejected;
      result.reason = json.dump();
      return result;
    }
    result.disposition = SubmitDisposition::Acknowledged;
    if (json.contains("result") && json["result"].contains("id")) {
      result.broker_order_id = json["result"]["id"].get<std::uint64_t>();
    }
    return result;
  } catch (...) {
    result.disposition = SubmitDisposition::Ambiguous;
    result.reason = "invalid_json";
    return result;
  }
}

[[nodiscard]] CancelResult parse_cancel_response(const HttpResponse& response) {
  CancelResult result{};
  if (response.status >= 500) {
    result.disposition = CancelDisposition::Ambiguous;
    result.reason = "server_error";
    return result;
  }
  if (response.status >= 400) {
    result.disposition = CancelDisposition::Rejected;
    result.reason = response.body;
    return result;
  }
  result.disposition = CancelDisposition::Confirmed;
  return result;
}

[[nodiscard]] QueryResult parse_query_response(const HttpResponse& response) {
  QueryResult result{};
  if (response.status == 404) {
    result.disposition = QueryDisposition::NotFound;
    return result;
  }
  if (response.status >= 400) {
    result.disposition = QueryDisposition::Error;
    result.reason = response.body;
    return result;
  }
  try {
    const auto json = nlohmann::json::parse(response.body);
    if (!json.contains("result")) {
      result.disposition = QueryDisposition::NotFound;
      return result;
    }
    const auto& row = json["result"];
    result.disposition = QueryDisposition::Found;
    if (row.contains("client_order_id")) {
      const std::string text = row["client_order_id"].get<std::string>();
      for (std::size_t i = 0; i < text.size() && i < result.order.client_id.bytes.size(); ++i) {
        result.order.client_id.bytes[i] = text[i];
      }
    }
    if (row.contains("id")) {
      result.order.broker_order_id = row["id"].get<std::uint64_t>();
    }
    if (row.contains("size")) {
      result.order.qty_lots = row["size"].get<std::int32_t>();
    }
    if (row.contains("state")) {
      const std::string state = row["state"].get<std::string>();
      if (state == "open") {
        result.order.state = orders::OrderState::Acked;
      } else if (state == "closed") {
        result.order.state = orders::OrderState::Filled;
      }
    }
    return result;
  } catch (...) {
    result.disposition = QueryDisposition::Error;
    result.reason = "invalid_json";
    return result;
  }
}

} // namespace

RestClient::RestClient(const RestClientConfig config) : config_(config) {
  if (config_.now_seconds == nullptr) {
    config_.now_seconds = default_now_seconds;
  }
}

void RestClient::set_http_transport(HttpTransport* transport) { http_ = transport; }

void RestClient::set_rate_limiter(CategoryRateLimiter* limiter) { limiter_ = limiter; }

core::Result<HttpResponse, BrokerError>
RestClient::send_signed(const std::string& method, const std::string& path, const std::string& query,
                         const std::string& body, const journal::v1::EndpointCategory category,
                         const RequestPriority priority, const std::int64_t now_mono_ns) {
  if (http_ == nullptr) {
    return core::Result<HttpResponse, BrokerError>::err(BrokerError::TransportFailed);
  }
  if (limiter_ != nullptr && !limiter_->admit(category, priority, now_mono_ns)) {
    return core::Result<HttpResponse, BrokerError>::err(BrokerError::RateLimited);
  }

  ApiCredentials credentials{};
  if (config_.credentials.load == nullptr ||
      !config_.credentials.load(config_.credentials.ctx, credentials)) {
    return core::Result<HttpResponse, BrokerError>::err(BrokerError::CredentialsMissing);
  }

  const std::string timestamp = std::to_string(config_.now_seconds());
  const std::string signature =
      sign_request(credentials.api_secret, method, timestamp, path, query, body);

  HttpRequest request{};
  request.method = method;
  request.path = path;
  request.query = query;
  request.body = body;
  request.headers["api-key"] = credentials.api_key;
  request.headers["signature"] = signature;
  request.headers["timestamp"] = timestamp;
  request.headers["User-Agent"] = "trading-engine-0.1.0";
  request.headers["Content-Type"] = "application/json";

  auto response = http_->send(request);
  if (!response.is_ok()) {
    return response;
  }
  if (response.value().status == 429 && limiter_ != nullptr) {
    limiter_->on_rate_limited(category, now_mono_ns, 1'000'000'000);
  }
  return response;
}

core::Result<SubmitResult, BrokerError> RestClient::place_order(const PlaceOrderRequest& request,
                                                                  const std::int64_t now_mono_ns) {
  nlohmann::json body{};
  body["product_id"] = request.product_id;
  body["size"] = request.size_lots;
  body["side"] = request.side == orders::Side::Buy ? "buy" : "sell";
  body["limit_price"] = std::to_string(request.limit_price_ticks);
  body["client_order_id"] = client_id_text(request.client_id);
  body["order_type"] = "limit_order";
  if (request.reduce_only) {
    body["reduce_only"] = true;
  }
  const std::string payload = body.dump();
  const RequestPriority priority =
      request.reduce_only ? RequestPriority::Reduction : RequestPriority::NewEntry;
  auto response = send_signed("POST", "/v2/orders", "", payload, category_for_place(request.reduce_only),
                               priority, now_mono_ns);
  if (!response.is_ok()) {
    return core::Result<SubmitResult, BrokerError>::err(response.error());
  }
  return core::Result<SubmitResult, BrokerError>::ok(parse_place_response(response.value()));
}

core::Result<CancelResult, BrokerError> RestClient::cancel_order(const orders::ClientOrderId& client_id,
                                                                  const std::int64_t now_mono_ns) {
  const std::string query = "?client_order_id=" + client_id_text(client_id);
  auto response = send_signed("DELETE", "/v2/orders", query, "", category_for_cancel(),
                               RequestPriority::Cancel, now_mono_ns);
  if (!response.is_ok()) {
    return core::Result<CancelResult, BrokerError>::err(response.error());
  }
  return core::Result<CancelResult, BrokerError>::ok(parse_cancel_response(response.value()));
}

core::Result<QueryResult, BrokerError> RestClient::query_order(const orders::ClientOrderId& client_id,
                                                                const std::int64_t now_mono_ns) {
  const std::string query = "?client_order_id=" + client_id_text(client_id);
  auto response = send_signed("GET", "/v2/orders", query, "", category_for_query(),
                               RequestPriority::NewEntry, now_mono_ns);
  if (!response.is_ok()) {
    return core::Result<QueryResult, BrokerError>::err(response.error());
  }
  return core::Result<QueryResult, BrokerError>::ok(parse_query_response(response.value()));
}

} // namespace trading::broker::delta
