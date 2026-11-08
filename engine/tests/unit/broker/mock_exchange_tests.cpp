#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "trading/broker/mock/adapter.hpp"
#include "trading/broker/mock/scenario.hpp"
#include "trading/orders/types.hpp"

#ifndef TRADING_REPO_ROOT
#error "TRADING_REPO_ROOT must be defined"
#endif

namespace fs = std::filesystem;

namespace {

fs::path repo_root() { return fs::path(TRADING_REPO_ROOT); }

fs::path scenario_path(const std::string& name) {
  return repo_root() / "tests/mock_exchange/scenarios" / name;
}

[[nodiscard]] trading::orders::ClientOrderId client_id(const std::string& text) {
  trading::orders::ClientOrderId id{};
  for (std::size_t i = 0; i < text.size() && i < id.bytes.size(); ++i) {
    id.bytes[i] = text[i];
  }
  return id;
}

[[nodiscard]] trading::broker::PlaceOrderRequest sample_place() {
  trading::broker::PlaceOrderRequest request{};
  request.client_id = client_id("S0-0000000001");
  request.product_id = 1;
  request.size_lots = 2;
  request.side = trading::orders::Side::Buy;
  request.limit_price_ticks = 100;
  return request;
}

trading::broker::mock::MockExchangeBroker broker_from(const std::string& scenario_file) {
  const auto loaded =
      trading::broker::mock::load_scenario_from_file(scenario_path(scenario_file).string());
  if (!loaded.is_ok()) {
    ADD_FAILURE() << "failed to load scenario";
    return trading::broker::mock::MockExchangeBroker{};
  }
  return trading::broker::mock::MockExchangeBroker(std::move(loaded.value()));
}

} // namespace

TEST(MockExchange, ImplementsIBroker) {
  trading::broker::mock::MockExchangeBroker broker{};
  ASSERT_TRUE(broker.connect());
  EXPECT_EQ(broker.health().state, trading::broker::BrokerHealthState::Connected);
  const auto submit = broker.place_order(sample_place());
  EXPECT_EQ(submit.disposition, trading::broker::SubmitDisposition::Acknowledged);
}

TEST(MockExchange, AckAndPartialFillFromToml) {
  auto broker = broker_from("ack_partial_fill.toml");
  ASSERT_TRUE(broker.connect());
  static_cast<void>(broker.poll_private_events());

  const auto submit = broker.place_order(sample_place());
  EXPECT_EQ(submit.disposition, trading::broker::SubmitDisposition::Acknowledged);
  EXPECT_EQ(submit.broker_order_id, 501U);

  broker.tick(500);
  auto events = broker.poll_private_events();
  EXPECT_TRUE(events.empty());

  broker.tick(1'500);
  events = broker.poll_private_events();
  ASSERT_FALSE(events.empty());

  const auto queried = broker.query_order(sample_place().client_id);
  ASSERT_EQ(queried.disposition, trading::broker::QueryDisposition::Found);
  EXPECT_EQ(queried.order.filled_qty_lots, 1);

  broker.tick(2'500);
  events = broker.poll_private_events();
  ASSERT_FALSE(events.empty());
  const auto final_query = broker.query_order(sample_place().client_id);
  EXPECT_EQ(final_query.order.filled_qty_lots, 2);
  EXPECT_EQ(final_query.order.state, trading::orders::OrderState::Filled);
}

TEST(MockExchange, RejectScenario) {
  auto broker = broker_from("reject.toml");
  ASSERT_TRUE(broker.connect());
  const auto submit = broker.place_order(sample_place());
  EXPECT_EQ(submit.disposition, trading::broker::SubmitDisposition::Rejected);
  EXPECT_EQ(submit.reason, "insufficient_margin");
}

TEST(MockExchange, TimeoutScenario) {
  auto broker = broker_from("timeout.toml");
  ASSERT_TRUE(broker.connect());
  const auto submit = broker.place_order(sample_place());
  EXPECT_EQ(submit.disposition, trading::broker::SubmitDisposition::Ambiguous);
  EXPECT_EQ(submit.reason, "timeout");
}

TEST(MockExchange, RateLimitedScenario) {
  auto broker = broker_from("rate_limited.toml");
  ASSERT_TRUE(broker.connect());
  const auto submit = broker.place_order(sample_place());
  EXPECT_EQ(submit.disposition, trading::broker::SubmitDisposition::Ambiguous);
  EXPECT_EQ(submit.reason, "rate_limited");
}

TEST(MockExchange, DisconnectScenario) {
  auto broker = broker_from("disconnect.toml");
  ASSERT_TRUE(broker.connect());

  const auto first = broker.place_order(sample_place());
  EXPECT_EQ(first.disposition, trading::broker::SubmitDisposition::Acknowledged);

  broker.tick(5'000);
  const auto events = broker.poll_private_events();
  ASSERT_FALSE(events.empty());
  EXPECT_EQ(events.back().kind, trading::broker::PrivateEventKind::Disconnected);
  EXPECT_EQ(broker.health().state, trading::broker::BrokerHealthState::Degraded);

  const auto second = broker.place_order(sample_place());
  EXPECT_EQ(second.disposition, trading::broker::SubmitDisposition::Ambiguous);
}

TEST(MockExchange, ScenarioReplayIsDeterministic) {
  auto first = broker_from("ack_partial_fill.toml");
  auto second = broker_from("ack_partial_fill.toml");
  ASSERT_TRUE(first.connect());
  ASSERT_TRUE(second.connect());
  const auto a = first.place_order(sample_place());
  const auto b = second.place_order(sample_place());
  EXPECT_EQ(a.broker_order_id, b.broker_order_id);
  EXPECT_EQ(first.place_calls(), second.place_calls());
}
