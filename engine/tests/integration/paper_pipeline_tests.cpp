#include "broker_execution.hpp"
#include "engine_pipeline.hpp"
#include "paper_paths.hpp"
#include "paper_session.hpp"
#include "strategies/flat_echo/strategy.hpp"
#include "trading/core/clock.hpp"
#include "trading/marketdata/pipeline_types.hpp"
#include "trading/orders/client_order_id.hpp"
#include "trading/risk/gate.hpp"
#include "trading/strategy/market_snapshot.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

#include <gtest/gtest.h>

#ifndef TRADING_REPO_ROOT
#error "TRADING_REPO_ROOT must be defined"
#endif

namespace {

namespace fs = std::filesystem;

using trading::orders::ClientOrderIdGenerator;
using trading::strategy::flat_echo::FlatEcho;
using trading::strategy::flat_echo::FlatParams;
using trading::trader::EnginePipeline;

struct JournalProbe {
  static constexpr std::size_t kMax = 32;
  trading::risk::DecisionRecord records[kMax]{};
  std::size_t count{0};
};

bool record_decision(void* ctx, const trading::risk::DecisionRecord& record) {
  auto* probe = static_cast<JournalProbe*>(ctx);
  if (probe->count >= JournalProbe::kMax) {
    return false;
  }
  probe->records[probe->count++] = record;
  return true;
}

bool persist_counter(void* /*ctx*/, std::uint64_t /*counter*/) { return true; }

trading::risk::PortfolioView calm_view() {
  trading::risk::PortfolioView view;
  view.traded_greeks_present = true;
  view.margin_headroom = 1'000'000;
  view.order_margin = 1;
  view.reference_price_ticks = 100;
  view.orders_in_window = 10;
  view.entries_in_window = 0;
  view.equity = 1'000'000;
  view.high_water = 1'000'000;
  return view;
}

trading::risk::LimitSet wide_limits() {
  trading::risk::LimitSet limits{};
  limits.max_order_notional = 1'000'000'000'000;
  limits.max_order_qty = 1'000'000;
  limits.max_net_instrument = 1'000'000;
  limits.max_net_underlying = 1'000'000;
  limits.max_gross_exposure = 1'000'000'000'000;
  limits.max_abs_delta = 1'000'000'000'000;
  limits.max_abs_gamma = 1'000'000'000'000;
  limits.max_abs_vega = 1'000'000'000'000;
  limits.collar_ticks = 1'000'000;
  limits.max_entries_per_window = 1'000'000;
  limits.max_cancel_ratio_bps = 10'000;
  limits.max_strategy_loss = 1'000'000'000'000;
  limits.max_portfolio_loss = 1'000'000'000'000;
  limits.max_drawdown = 1'000'000'000'000;
  limits.min_margin_headroom = 0;
  return limits;
}

void wait_until(const auto& predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
}

} // namespace

TEST(PaperPipeline, PaperProfileSelectsMockAdapter) {
  const fs::path config_root = fs::path(TRADING_REPO_ROOT) / "config";
  trading::trader::PaperTradingSession session;
  ASSERT_FALSE(session.load_profile(config_root, "paper", false, true));
  EXPECT_EQ(session.startup().profile_mode, "paper");
  EXPECT_EQ(session.startup().execution_adapter, "mock");
}

TEST(PaperPipeline, StartupReconcileAgainstMockBroker) {
  const fs::path config_root = fs::path(TRADING_REPO_ROOT) / "config";
  trading::trader::PaperTradingSession session;
  ASSERT_FALSE(session.load_profile(config_root, "paper", false, true));
  const auto scenario = trading::trader::default_paper_mock_scenario(config_root);
  const auto boot = session.bootstrap_mock_broker(scenario, trading::core::Clock::monotonicNs());
  ASSERT_TRUE(boot.ok) << boot.detail;
  EXPECT_TRUE(session.reconciler().startup_reconcile_clean());
  EXPECT_TRUE(session.reconciler().trading_allowed());
}

TEST(PaperPipeline, QuoteThroughMockFill) {
  const fs::path config_root = fs::path(TRADING_REPO_ROOT) / "config";
  trading::trader::PaperTradingSession session;
  ASSERT_FALSE(session.load_profile(config_root, "paper", false, true));
  const auto scenario = trading::trader::default_paper_mock_scenario(config_root);
  ASSERT_TRUE(session.bootstrap_mock_broker(scenario, trading::core::Clock::monotonicNs()).ok);

  JournalProbe journal_probe{};
  auto gate = trading::risk::RiskGate::with_fixture_limits(wide_limits());
  trading::risk::JournalPort port{};
  port.ctx = &journal_probe;
  port.record = record_decision;
  gate.set_journal(port);
  gate.on_session_start();

  ClientOrderIdGenerator id_source("PP03", 1, persist_counter, nullptr);
  FlatEcho strategy(trading::strategy::flat_echo::kSampleStrategyId);

  FlatParams params{};
  params.version = 4;
  params.instrument_id = 42;
  params.qty_lots = 2;
  params.side = trading::strategy::Side::Buy;
  params.time_in_force = trading::strategy::TimeInForce::Day;

  EnginePipeline::JournalDrain drain{};
  drain.ctx = &journal_probe;
  drain.on_record = [](void* ctx, const trading::risk::DecisionRecord& record) {
    static_cast<void>(record_decision(ctx, record));
  };

  EnginePipeline pipeline(strategy, gate, id_source);
  pipeline.set_journal_drain(drain);
  pipeline.set_portfolio_view(calm_view());
  pipeline.set_param_blob(&params, sizeof(params), params.version);
  pipeline.set_broker(&session.broker());
  pipeline.start_and_warmup(0);

  trading::marketdata::QuoteIngress quote{};
  quote.sequence = 88;
  quote.quote = trading::strategy::Quote{
      .instrument_id = 42, .bid_ticks = 10, .ask_ticks = 12, .bid_size_lots = 5, .ask_size_lots = 5};

  ASSERT_TRUE(pipeline.submit_quote(quote));
  wait_until([&] { return session.mock_broker().place_calls() >= 1u; }, std::chrono::seconds(5));
  EXPECT_GE(journal_probe.count, 1u);

  pipeline.request_stop();
  pipeline.join();

  EXPECT_GE(session.mock_broker().place_calls(), 1u);

  trading::orders::ClientOrderId client_id{};
  const auto& last = pipeline.execution_sink().last;
  for (std::size_t i = 0; i < client_id.bytes.size(); ++i) {
    client_id.bytes[i] = last.client_id[i];
  }

  session.broker().tick(trading::core::Clock::monotonicNs());
  const auto queried = session.broker().query_order(client_id);
  EXPECT_EQ(queried.disposition, trading::broker::QueryDisposition::Found);
  EXPECT_EQ(queried.order.filled_qty_lots, 2);
}
