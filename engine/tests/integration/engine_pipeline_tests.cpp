#include "engine_pipeline.hpp"
#include "strategies/flat_echo/strategy.hpp"
#include "trading/core/allocation_hook.hpp"
#include "trading/core/clock.hpp"
#include "trading/core/thread_pin.hpp"
#include "trading/marketdata/pipeline_types.hpp"
#include "trading/risk/gate.hpp"
#include "trading/strategy/market_snapshot.hpp"

#include <chrono>
#include <cstring>
#include <thread>

#include <gtest/gtest.h>

namespace {

using trading::core::AllocationGuard;
using trading::marketdata::QuoteIngress;
using trading::orders::ClientOrderIdGenerator;
using trading::risk::DecisionRecord;
using trading::risk::JournalPort;
using trading::risk::LimitSet;
using trading::risk::PortfolioView;
using trading::strategy::Quote;
using trading::strategy::Side;
using trading::strategy::TimeInForce;
using trading::strategy::flat_echo::FlatEcho;
using trading::strategy::flat_echo::FlatParams;
using trading::trader::EnginePipeline;

struct JournalProbe {
  static constexpr std::size_t kMax = 32;
  DecisionRecord records[kMax]{};
  std::size_t count{0};
};

bool record_decision(void* ctx, const DecisionRecord& record) {
  auto* probe = static_cast<JournalProbe*>(ctx);
  if (probe->count >= JournalProbe::kMax) {
    return false;
  }
  probe->records[probe->count++] = record;
  return true;
}

bool persist_counter(void* /*ctx*/, std::uint64_t /*counter*/) { return true; }

PortfolioView calm_view() {
  PortfolioView view;
  view.traded_greeks_present = true;
  view.margin_headroom = 1'000'000;
  view.order_margin = 1;
  view.reference_price_ticks = 100;
  view.orders_in_window = 10;
  view.cancels_in_window = 0;
  view.entries_in_window = 0;
  view.equity = 1'000'000;
  view.high_water = 1'000'000;
  view.strategy_pnl = 0;
  view.portfolio_pnl = 0;
  return view;
}

LimitSet wide_limits() {
  LimitSet limits{};
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

TEST(EnginePipeline, QuoteToExecutionWithApprovalToken) {
  JournalProbe journal_probe{};
  auto gate = trading::risk::RiskGate::with_fixture_limits(wide_limits());
  JournalPort port{};
  port.ctx = &journal_probe;
  port.record = record_decision;
  gate.set_journal(port);
  gate.on_session_start();

  ClientOrderIdGenerator id_source("TST1", 1, persist_counter, nullptr);
  FlatEcho strategy(trading::strategy::flat_echo::kSampleStrategyId);

  FlatParams params{};
  params.version = 4;
  params.instrument_id = 42;
  params.qty_lots = 2;
  params.side = Side::Buy;
  params.time_in_force = TimeInForce::Day;

  const auto before_pins = trading::core::pinThreadWarningCount();
  EnginePipeline pipeline(strategy, gate, id_source);
  pipeline.set_portfolio_view(calm_view());
  pipeline.set_param_blob(&params, sizeof(params), params.version);
  pipeline.start_and_warmup(3);

#if !defined(__linux__)
  EXPECT_GE(trading::core::pinThreadWarningCount(), before_pins + 5);
#endif

  const std::uint32_t baseline = pipeline.execution_sink().completed.load(std::memory_order_acquire);

  QuoteIngress quote{};
  quote.sequence = 99;
  quote.quote =
      Quote{.instrument_id = 42, .bid_ticks = 10, .ask_ticks = 12, .bid_size_lots = 5, .ask_size_lots = 5};

  ASSERT_TRUE(pipeline.submit_quote(quote));
  wait_until(
      [&] {
        return pipeline.execution_sink().completed.load(std::memory_order_acquire) > baseline;
      },
      std::chrono::seconds(3));

  const auto& sink = pipeline.execution_sink();
  EXPECT_EQ(sink.last.intent.instrument_id, 42u);
  EXPECT_EQ(sink.last.intent.qty_lots, 2);
  EXPECT_EQ(sink.last.intent.price_ticks, 12);
  EXPECT_GT(sink.last.token.token_id, 0u);
  EXPECT_GT(sink.last.token.expiry_mono_ns, trading::core::Clock::monotonicNs());

  pipeline.request_stop();
  pipeline.join();
  EXPECT_GE(journal_probe.count, 1u);
  EXPECT_GE(pipeline.journal_drained(), 1u);
}

TEST(EnginePipeline, HotPathZeroAllocationsAfterWarmup) {
  JournalProbe journal_probe{};
  auto gate = trading::risk::RiskGate::with_fixture_limits(wide_limits());
  JournalPort port{};
  port.ctx = &journal_probe;
  port.record = record_decision;
  gate.set_journal(port);
  gate.on_session_start();

  ClientOrderIdGenerator id_source("TST1", 1, persist_counter, nullptr);
  FlatEcho strategy(trading::strategy::flat_echo::kSampleStrategyId);

  FlatParams params{};
  params.version = 4;
  params.instrument_id = 42;
  params.qty_lots = 2;
  params.side = Side::Buy;

  EnginePipeline pipeline(strategy, gate, id_source);
  pipeline.set_portfolio_view(calm_view());
  pipeline.set_param_blob(&params, sizeof(params), params.version);
  pipeline.start_and_warmup(4);

  auto submit_buy_quote = [&](std::uint64_t sequence) {
    QuoteIngress q{};
    q.sequence = sequence;
    q.quote = Quote{.instrument_id = 42, .bid_ticks = 10, .ask_ticks = 12};
    ASSERT_TRUE(pipeline.submit_quote(q));
  };

  std::uint32_t baseline = pipeline.execution_sink().completed.load(std::memory_order_acquire);
  for (std::uint64_t seq = 200; seq < 203; ++seq) {
    submit_buy_quote(seq);
    const std::uint32_t target = baseline + 1;
    wait_until(
        [&] { return pipeline.execution_sink().completed.load(std::memory_order_acquire) >= target; },
        std::chrono::seconds(3));
    baseline = target;
  }

  AllocationGuard guard;

  QuoteIngress quote{};
  quote.sequence = 300;
  quote.quote = Quote{.instrument_id = 42, .bid_ticks = 10, .ask_ticks = 12};

  ASSERT_TRUE(pipeline.submit_quote(quote));
  baseline = pipeline.execution_sink().completed.load(std::memory_order_acquire);
  wait_until(
      [&] {
        return pipeline.execution_sink().completed.load(std::memory_order_acquire) > baseline;
      },
      std::chrono::seconds(3));

  EXPECT_EQ(AllocationGuard::count(), 0);

  pipeline.request_stop();
  pipeline.join();
}

TEST(EnginePipeline, ExecutionRejectsExpiredToken) {
  JournalProbe journal_probe{};
  auto gate = trading::risk::RiskGate::with_fixture_limits(wide_limits());
  JournalPort port{};
  port.ctx = &journal_probe;
  port.record = record_decision;
  gate.set_journal(port);
  gate.on_session_start();

  ClientOrderIdGenerator id_source("TST1", 1, persist_counter, nullptr);
  const auto id = id_source.next();
  ASSERT_TRUE(id.is_ok());

  trading::risk::GateRequest request{};
  request.intent.strategy_id = trading::strategy::flat_echo::kSampleStrategyId;
  request.intent.instrument_id = 42;
  request.intent.side = Side::Buy;
  request.intent.qty_lots = 2;
  request.intent.price_ticks = 12;
  for (std::size_t i = 0; i < request.client_id.size(); ++i) {
    request.client_id[i] = id.value().bytes[i];
  }

  const std::int64_t issued_at = trading::core::Clock::monotonicNs();
  const auto approved = gate.evaluate(request, calm_view(), issued_at);
  ASSERT_EQ(approved.status, trading::risk::GateStatus::Approved);

  std::this_thread::sleep_for(std::chrono::milliseconds(600));

  trading::risk::ApprovedIngress packet{};
  packet.intent = request.intent;
  packet.client_id = request.client_id;
  packet.token = approved.token;

  trading::orders::ExecutionSink sink{};
  const std::int64_t now = trading::core::Clock::monotonicNs();
  ASSERT_FALSE(trading::orders::accept_for_execution(packet, gate, now, sink));
  EXPECT_EQ(sink.rejected_token.load(std::memory_order_acquire), 1u);
}

} // namespace
