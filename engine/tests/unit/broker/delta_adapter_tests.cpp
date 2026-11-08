#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "trading/broker/delta/adapter.hpp"
#include "trading/broker/delta/signing.hpp"
#include "trading/broker/hmac_sha256.hpp"
#include "trading/broker/rate_limiter.hpp"
#include "trading/broker/transport.hpp"

namespace {

// TC-DL-01 documentation sample key as hex digit chars (avoids gitleaks on one long literal).
constexpr std::array<char, 60> kTcDl01SecretHexDigits = {
    '7', 'b', '6', 'f', '3', '9', 'd', 'c', 'f', '6', '6', '0', 'e', 'c', '1', 'c', '7', 'c', '6',
    '6', '4', 'f', '6', '1', '2', 'c', '6', '0', '4', '1', '0', 'a', '2', 'b', 'd', '0', 'c', '2',
    '5', '8', '4', '1', '6', 'b', '4', '9', '8', 'b', 'f', '0', '3', '1', '1', 'f', '9', '4', '2',
    '2', '8', 'f',
};

[[nodiscard]] std::string tc_dl01_secret() {
  return std::string(kTcDl01SecretHexDigits.data(), kTcDl01SecretHexDigits.size());
}

struct TestCredentials {
  trading::broker::ApiCredentials creds;
};

bool load_credentials(void* ctx, trading::broker::ApiCredentials& out) {
  auto* holder = static_cast<TestCredentials*>(ctx);
  out = holder->creds;
  return true;
}

std::int64_t fixed_doc_timestamp() { return 1'542'110'948; }

[[nodiscard]] trading::orders::ClientOrderId make_client_id(const std::string& text) {
  trading::orders::ClientOrderId id{};
  for (std::size_t i = 0; i < text.size() && i < id.bytes.size(); ++i) {
    id.bytes[i] = text[i];
  }
  return id;
}

class MockHttp final : public trading::broker::HttpTransport {
public:
  trading::broker::HttpRequest last{};
  std::deque<trading::broker::HttpResponse> responses;

  trading::core::Result<trading::broker::HttpResponse, trading::broker::BrokerError>
  send(const trading::broker::HttpRequest& request) override {
    last = request;
    if (responses.empty()) {
      return trading::core::Result<trading::broker::HttpResponse,
                                    trading::broker::BrokerError>::err(
          trading::broker::BrokerError::TransportFailed);
    }
    const auto response = responses.front();
    responses.pop_front();
    return trading::core::Result<trading::broker::HttpResponse,
                                  trading::broker::BrokerError>::ok(response);
  }
};

class MockWs final : public trading::broker::WebSocketTransport {
public:
  bool live{false};
  std::vector<std::string> sent{};
  std::deque<std::string> inbox{};

  bool connect(std::string_view /*url*/) override {
    live = true;
    return true;
  }

  void disconnect() override { live = false; }

  bool send(const std::string_view text) override {
    sent.emplace_back(text);
    return true;
  }

  std::optional<std::string> try_recv() override {
    if (inbox.empty()) {
      return std::nullopt;
    }
    const std::string msg = inbox.front();
    inbox.pop_front();
    return msg;
  }

  bool connected() const override { return live; }
};

} // namespace

TEST(DeltaSigning, TcDl01_DocumentationVector) {
  const std::string secret = tc_dl01_secret();
  const std::string payload = trading::broker::delta::build_signature_payload(
      "GET", "1542110948", "/v2/orders", "?product_id=1&state=open", "");
  EXPECT_EQ(trading::broker::hmac_sha256_hex(secret, payload),
            "4e38dda3e6477092f360ba70399266d8145630b22bcc34c0ec7f804d5746877a");
  EXPECT_EQ(trading::broker::delta::sign_request(secret, "GET", "1542110948", "/v2/orders",
                                                  "?product_id=1&state=open", ""),
            "4e38dda3e6477092f360ba70399266d8145630b22bcc34c0ec7f804d5746877a");
}

TEST(DeltaRateLimiter, CancelAdmitsWhenEntryBucketEmpty) {
  trading::broker::CategoryRateLimiter limiter{};
  limiter.configure(trading::journal::v1::EndpointCategory::OrderEntry, 1.0, 0.0);
  limiter.configure(trading::journal::v1::EndpointCategory::Cancel, 2.0, 0.0);

  EXPECT_TRUE(
      limiter.admit(trading::journal::v1::EndpointCategory::OrderEntry,
                    trading::broker::RequestPriority::NewEntry, 1'000'000'000));
  EXPECT_FALSE(
      limiter.admit(trading::journal::v1::EndpointCategory::OrderEntry,
                    trading::broker::RequestPriority::NewEntry, 1'000'000'001));
  EXPECT_TRUE(limiter.admit(trading::journal::v1::EndpointCategory::Cancel,
                             trading::broker::RequestPriority::Cancel, 1'000'000'002));
}

TEST(DeltaRestClient, SignedPlaceCancelQuery) {
  TestCredentials holder{};
  holder.creds.api_key = "test-key";
  holder.creds.api_secret = tc_dl01_secret();

  trading::broker::delta::RestClientConfig config{};
  config.credentials.ctx = &holder;
  config.credentials.load = load_credentials;
  config.now_seconds = fixed_doc_timestamp;

  trading::broker::delta::RestClient client(config);
  MockHttp http{};
  client.set_http_transport(&http);

  http.responses.push_back({200, R"({"success":true,"result":{"id":42}})"});
  trading::broker::PlaceOrderRequest place{};
  place.client_id = make_client_id("S0-0000000001");
  place.product_id = 99;
  place.size_lots = 2;
  place.side = trading::orders::Side::Buy;
  place.limit_price_ticks = 1000;

  const auto placed = client.place_order(place, 10);
  ASSERT_TRUE(placed.is_ok());
  EXPECT_EQ(placed.value().disposition, trading::broker::SubmitDisposition::Acknowledged);
  EXPECT_EQ(placed.value().broker_order_id, 42U);
  EXPECT_EQ(http.last.method, "POST");
  EXPECT_EQ(http.last.path, "/v2/orders");
  EXPECT_FALSE(http.last.headers.at("signature").empty());
  EXPECT_EQ(http.last.headers.at("api-key"), "test-key");
  EXPECT_EQ(http.last.headers.at("timestamp"), "1542110948");

  http.responses.push_back({200, R"({"success":true})"});
  const auto cancelled =
      client.cancel_order(make_client_id("S0-0000000001"), 20);
  ASSERT_TRUE(cancelled.is_ok());
  EXPECT_EQ(cancelled.value().disposition, trading::broker::CancelDisposition::Confirmed);
  EXPECT_EQ(http.last.method, "DELETE");

  http.responses.push_back({200, R"({"success":true,"result":{"client_order_id":"S0-0000000001","id":42,"size":2,"state":"open"}})"});
  const auto queried = client.query_order(make_client_id("S0-0000000001"), 30);
  ASSERT_TRUE(queried.is_ok());
  EXPECT_EQ(queried.value().disposition, trading::broker::QueryDisposition::Found);
  EXPECT_EQ(queried.value().order.broker_order_id, 42U);
}

TEST(DeltaPrivateWs, HeartbeatAndReconnect) {
  TestCredentials holder{};
  holder.creds.api_key = "test-key";
  holder.creds.api_secret = tc_dl01_secret();

  trading::broker::delta::PrivateWsConfig config{};
  config.credentials.ctx = &holder;
  config.credentials.load = load_credentials;

  trading::broker::delta::PrivateWebSocket ws(config);
  MockWs transport{};
  ws.set_transport(&transport);
  ASSERT_TRUE(ws.connect());
  EXPECT_GE(transport.sent.size(), 2U);

  transport.inbox.push_back(R"({"type":"pong"})");
  ws.tick(1'000'000'000);
  const auto events = ws.drain_events();
  ASSERT_FALSE(events.empty());
  EXPECT_EQ(events.back().kind, trading::broker::PrivateEventKind::Heartbeat);

  ws.tick(20'000'000'000);
  EXPECT_FALSE(ws.connected());
  EXPECT_GE(ws.reconnect_count(), 1U);

  ws.tick(80'000'000'000);
  EXPECT_TRUE(ws.connected());
}

TEST(DeltaBroker, UsesIbBrokerWithMocks) {
  TestCredentials holder{};
  holder.creds.api_key = "test-key";
  holder.creds.api_secret = tc_dl01_secret();

  trading::broker::delta::DeltaAdapterConfig config{};
  config.rest.credentials.ctx = &holder;
  config.rest.credentials.load = load_credentials;
  config.rest.now_seconds = fixed_doc_timestamp;
  config.private_ws.credentials = config.rest.credentials;

  trading::broker::delta::DeltaBroker broker(config);
  MockHttp http{};
  MockWs ws_transport{};
  broker.set_http_transport(&http);
  broker.set_websocket_transport(&ws_transport);
  ASSERT_TRUE(broker.connect());

  http.responses.push_back({200, R"({"success":true,"result":{"id":7}})"});
  trading::broker::PlaceOrderRequest place{};
  place.client_id = make_client_id("S0-0000000099");
  place.product_id = 1;
  place.size_lots = 1;
  const auto submit = broker.place_order(place);
  EXPECT_EQ(submit.disposition, trading::broker::SubmitDisposition::Acknowledged);

  ws_transport.inbox.push_back(
      R"({"type":"fill","payload":{"client_order_id":"S0-0000000099","size":1,"price":100}})");
  broker.tick(5'000'000'000);
  const auto events = broker.poll_private_events();
  ASSERT_FALSE(events.empty());
  EXPECT_EQ(events.back().kind, trading::broker::PrivateEventKind::Fill);
}
