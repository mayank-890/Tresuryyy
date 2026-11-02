#include "limits.hpp"
#include "trading/risk/gate.hpp"
#include "trading/strategy/intent.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using trading::risk::ApprovalToken;
using trading::risk::Decision;
using trading::risk::DecisionRecord;
using trading::risk::GateRequest;
using trading::risk::GateResult;
using trading::risk::GateStatus;
using trading::risk::JournalPort;
using trading::risk::LatchState;
using trading::risk::LimitId;
using trading::risk::LimitSet;
using trading::risk::PortfolioView;
using trading::risk::Position;
using trading::risk::RequestKind;
using trading::risk::RestingOrder;
using trading::risk::RiskGate;
using trading::risk::TokenCheck;
using trading::risk::can_tighten;
using trading::risk::decide;
using trading::risk::kApprovalTtlNs;
using trading::strategy::OrderIntent;
using trading::strategy::Side;

struct Probe {
  bool accept{true};
  std::vector<DecisionRecord> records;
};

bool record_decision(void* ctx, const DecisionRecord& record) {
  auto* probe = static_cast<Probe*>(ctx);
  if (!probe->accept) {
    return false;
  }
  probe->records.push_back(record);
  return true;
}

JournalPort journal_of(Probe* probe) {
  JournalPort port;
  port.ctx = probe;
  port.record = record_decision;
  return port;
}

std::array<char, 24> client(std::string_view text) {
  std::array<char, 24> out{};
  const std::size_t count = text.size() < out.size() ? text.size() : out.size();
  for (std::size_t i = 0; i < count; ++i) {
    out[i] = text[i];
  }
  return out;
}

// Fixture ceilings for boundary tests. Not operator limits and not written
// to limits.hpp.
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
  return view;
}

GateRequest make_order(std::int32_t qty, std::int64_t price, Side side = Side::Buy) {
  GateRequest request;
  request.client_id = client("CID-1");
  request.intent.strategy_id = 7;
  request.intent.instrument_id = 11;
  request.intent.side = side;
  request.intent.qty_lots = qty;
  request.intent.price_ticks = price;
  request.underlying_id = 3;
  request.kind = RequestKind::Order;
  return request;
}

Decision eval(const GateRequest& request, const PortfolioView& view, const LimitSet& limits,
              const LatchState& latches = {}) {
  return decide(request, view, limits, latches);
}

void expect_reject(const Decision& decision, LimitId limit, std::int64_t observed,
                   std::int64_t threshold) {
  EXPECT_FALSE(decision.approved);
  EXPECT_FALSE(decision.invalid_input);
  EXPECT_EQ(decision.limit, limit);
  EXPECT_EQ(decision.observed, observed);
  EXPECT_EQ(decision.threshold, threshold);
}

template <typename T>
concept HasApprovalToken = requires(T value) { value.token; };

static_assert(!HasApprovalToken<OrderIntent>);

} // namespace

TEST(RiskLimitsFile, RemainsOperatorTodos) {
  const std::filesystem::path path =
      std::filesystem::path(TRADING_REPO_ROOT) / "engine/src/risk/limits.hpp";
  std::ifstream input(path);
  ASSERT_TRUE(input.good());
  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  EXPECT_EQ(text.find("constexpr"), std::string::npos);
  for (int id = 1; id <= 16; ++id) {
    const std::string marker =
        std::string("TODO(operator): RL-") + (id < 10 ? "0" : "") + std::to_string(id);
    EXPECT_NE(text.find(marker), std::string::npos) << marker;
  }
}

TEST(RiskGate, StrategySourcesCannotMintTokens) {
  const std::filesystem::path roots[] = {
      std::filesystem::path(TRADING_REPO_ROOT) / "engine/include/trading/strategy",
      std::filesystem::path(TRADING_REPO_ROOT) / "engine/src/strategy",
  };
  int files = 0;
  for (const std::filesystem::path& root : roots) {
    ASSERT_TRUE(std::filesystem::exists(root));
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(root)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const auto ext = entry.path().extension();
      if (ext != ".hpp" && ext != ".cpp" && ext != ".h") {
        continue;
      }
      std::ifstream input(entry.path());
      ASSERT_TRUE(input.good());
      const std::string text((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
      ++files;
      EXPECT_EQ(text.find("ApprovalToken"), std::string::npos) << entry.path();
      EXPECT_EQ(text.find("with_fixture_limits"), std::string::npos) << entry.path();
    }
  }
  EXPECT_GT(files, 0);
}

TEST(RiskGate, UnconfiguredGateRefusesOrders) {
  Probe probe;
  RiskGate gate;
  gate.set_journal(journal_of(&probe));
  const GateResult result = gate.evaluate(make_order(1, 100), calm_view(), 1'000);
  EXPECT_EQ(result.status, GateStatus::NotConfigured);
  EXPECT_EQ(result.token.token_id, 0U);
  ASSERT_EQ(probe.records.size(), 1U);
  EXPECT_FALSE(probe.records[0].approved);
  EXPECT_EQ(probe.records[0].limit, LimitId::None);
  EXPECT_FALSE(gate.configured());
}

TEST(RiskGate, DecisionsAreDeterministic) {
  const GateRequest request = make_order(4, 125);
  const PortfolioView view = calm_view();
  LimitSet limits = wide_limits();
  limits.max_order_qty = 3;
  const Decision first = eval(request, view, limits);
  const Decision second = eval(request, view, limits);
  EXPECT_EQ(first.approved, second.approved);
  EXPECT_EQ(first.invalid_input, second.invalid_input);
  EXPECT_EQ(first.limit, second.limit);
  EXPECT_EQ(first.observed, second.observed);
  EXPECT_EQ(first.threshold, second.threshold);
  EXPECT_EQ(first.kill_edge, second.kill_edge);
  expect_reject(first, LimitId::Rl02, 4, 3);
}

TEST(RiskGate, Rl01NotionalBoundaries) {
  PortfolioView view = calm_view();
  LimitSet limits = wide_limits();
  limits.max_order_notional = 200;
  EXPECT_TRUE(eval(make_order(1, 199), view, limits).approved);
  EXPECT_TRUE(eval(make_order(2, 100), view, limits).approved);
  expect_reject(eval(make_order(2, 101), view, limits), LimitId::Rl01, 202, 200);

  limits.max_order_notional = 100;
  const Decision zero_price = eval(make_order(1, 0), view, limits);
  EXPECT_TRUE(zero_price.approved);

  limits = wide_limits();
  const Decision overflow =
      eval(make_order(2, std::numeric_limits<std::int64_t>::max()), view, limits);
  expect_reject(overflow, LimitId::Rl01, std::numeric_limits<std::int64_t>::max(),
                limits.max_order_notional);
}

TEST(RiskGate, Rl02QuantityBoundaries) {
  PortfolioView view = calm_view();
  LimitSet limits = wide_limits();
  limits.max_order_qty = 10;
  EXPECT_TRUE(eval(make_order(9, 100), view, limits).approved);
  EXPECT_TRUE(eval(make_order(10, 100), view, limits).approved);
  expect_reject(eval(make_order(11, 100), view, limits), LimitId::Rl02, 11, 10);

  const Decision zero = eval(make_order(0, 100), view, limits);
  EXPECT_TRUE(zero.invalid_input);
  EXPECT_FALSE(zero.approved);
  const Decision negative = eval(make_order(-3, 100), view, limits);
  EXPECT_TRUE(negative.invalid_input);
  EXPECT_EQ(negative.observed, -3);
}

TEST(RiskGate, NotionalFailsBeforeQuantity) {
  PortfolioView view = calm_view();
  LimitSet limits = wide_limits();
  limits.max_order_notional = 50;
  limits.max_order_qty = 1;
  expect_reject(eval(make_order(2, 100), view, limits), LimitId::Rl01, 200, 50);
}

TEST(RiskGate, Rl03PositionBoundaries) {
  Position book[] = {Position{11, 3, 0, 100}};
  PortfolioView view = calm_view();
  view.positions = book;
  LimitSet limits = wide_limits();
  limits.max_net_instrument = 8;
  EXPECT_TRUE(eval(make_order(7, 100), view, limits).approved);
  EXPECT_TRUE(eval(make_order(8, 100), view, limits).approved);
  expect_reject(eval(make_order(9, 100), view, limits), LimitId::Rl03, 9, 8);

  book[0].net_qty_lots = 10;
  EXPECT_TRUE(eval(make_order(1, 100, Side::Sell), view, limits).approved);
  EXPECT_TRUE(eval(make_order(12, 100, Side::Sell), view, limits).approved);
  book[0].net_qty_lots = 5;
  limits.max_net_instrument = 8;
  expect_reject(eval(make_order(20, 100, Side::Sell), view, limits), LimitId::Rl03, 15, 8);
}

TEST(RiskGate, Rl04UnderlyingBoundaries) {
  Position book[] = {Position{11, 3, 4, 10}, Position{12, 3, 4, 10}};
  PortfolioView view = calm_view();
  view.positions = book;
  view.reference_price_ticks = 10;
  LimitSet limits = wide_limits();
  limits.max_net_underlying = 10;
  EXPECT_TRUE(eval(make_order(1, 10), view, limits).approved);
  limits.max_net_underlying = 9;
  EXPECT_TRUE(eval(make_order(1, 10), view, limits).approved);
  limits.max_net_underlying = 8;
  expect_reject(eval(make_order(1, 10), view, limits), LimitId::Rl04, 9, 8);
}

TEST(RiskGate, Rl05GrossExposureBoundaries) {
  Position book[] = {Position{11, 3, 5, 10}, Position{12, 3, -4, 10}};
  PortfolioView view = calm_view();
  view.positions = book;
  view.reference_price_ticks = 10;
  LimitSet limits = wide_limits();
  limits.max_gross_exposure = 150;
  EXPECT_TRUE(eval(make_order(1, 10), view, limits).approved);
  limits.max_gross_exposure = 100;
  EXPECT_TRUE(eval(make_order(1, 10), view, limits).approved);
  limits.max_gross_exposure = 99;
  expect_reject(eval(make_order(1, 10), view, limits), LimitId::Rl05, 100, 99);
}

TEST(RiskGate, Rl06GreeksBoundaries) {
  PortfolioView view = calm_view();
  view.net_delta = 0;
  view.traded_delta_per_lot = 10;
  LimitSet limits = wide_limits();
  limits.max_abs_delta = 21;
  EXPECT_TRUE(eval(make_order(2, 100), view, limits).approved);
  limits.max_abs_delta = 20;
  EXPECT_TRUE(eval(make_order(2, 100), view, limits).approved);
  limits.max_abs_delta = 19;
  expect_reject(eval(make_order(2, 100), view, limits), LimitId::Rl06, 20, 19);

  view.net_gamma = 0;
  view.traded_gamma_per_lot = 5;
  view.traded_delta_per_lot = 0;
  limits = wide_limits();
  limits.max_abs_gamma = 4;
  expect_reject(eval(make_order(1, 100), view, limits), LimitId::Rl06, 5, 4);
  limits.max_abs_gamma = 5;
  EXPECT_TRUE(eval(make_order(1, 100), view, limits).approved);

  view.net_vega = 0;
  view.traded_gamma_per_lot = 0;
  view.traded_vega_per_lot = 3;
  limits = wide_limits();
  limits.max_abs_vega = 2;
  expect_reject(eval(make_order(1, 100), view, limits), LimitId::Rl06, 3, 2);

  view = calm_view();
  view.traded_greeks_present = false;
  expect_reject(eval(make_order(1, 100), view, wide_limits()), LimitId::Rl06, 0, 0);

  view = calm_view();
  view.net_delta = 30;
  view.traded_delta_per_lot = 1;
  limits = wide_limits();
  limits.max_abs_delta = 10;
  EXPECT_TRUE(eval(make_order(1, 100, Side::Sell), view, limits).approved);
  expect_reject(eval(make_order(1, 100), view, limits), LimitId::Rl06, 31, 10);
}

TEST(RiskGate, Rl07CollarBoundaries) {
  PortfolioView view = calm_view();
  view.reference_price_ticks = 1000;
  LimitSet limits = wide_limits();
  limits.collar_ticks = 10;
  EXPECT_TRUE(eval(make_order(1, 1009), view, limits).approved);
  EXPECT_TRUE(eval(make_order(1, 1010), view, limits).approved);
  expect_reject(eval(make_order(1, 1011), view, limits), LimitId::Rl07, 11, 10);

  view.reference_stale = true;
  expect_reject(eval(make_order(1, 1000), view, limits), LimitId::Rl07, 1000, 10);
  view.reference_stale = false;
  view.reference_price_ticks = 0;
  expect_reject(eval(make_order(1, 100), view, limits), LimitId::Rl07, 100, 10);
}

TEST(RiskGate, Rl08RateBudget) {
  PortfolioView view = calm_view();
  LimitSet limits = wide_limits();
  limits.max_entries_per_window = 2;
  view.entries_in_window = 0;
  EXPECT_TRUE(eval(make_order(1, 100), view, limits).approved);
  view.entries_in_window = 1;
  EXPECT_TRUE(eval(make_order(1, 100), view, limits).approved);
  view.entries_in_window = 2;
  expect_reject(eval(make_order(1, 100), view, limits), LimitId::Rl08, 3, 2);

  Position book[] = {Position{11, 3, 10, 100}};
  view.positions = book;
  EXPECT_TRUE(eval(make_order(1, 100, Side::Sell), view, limits).approved);

  GateRequest cancel = make_order(1, 100);
  cancel.kind = RequestKind::Cancel;
  EXPECT_TRUE(eval(cancel, view, limits).approved);
}

TEST(RiskGate, Rl09CancelRatio) {
  PortfolioView view = calm_view();
  view.orders_in_window = 10;
  LimitSet limits = wide_limits();
  limits.max_cancel_ratio_bps = 2000;
  view.cancels_in_window = 1;
  EXPECT_TRUE(eval(make_order(1, 100), view, limits).approved);
  view.cancels_in_window = 2;
  EXPECT_TRUE(eval(make_order(1, 100), view, limits).approved);
  view.cancels_in_window = 3;
  expect_reject(eval(make_order(1, 100), view, limits), LimitId::Rl09, 3000, 2000);
  view.cancels_in_window = 0;
  view.orders_in_window = 4;
  EXPECT_TRUE(eval(make_order(1, 100), view, limits).approved);
}

TEST(RiskGate, Rl10DailyLoss) {
  PortfolioView view = calm_view();
  LimitSet limits = wide_limits();
  limits.max_strategy_loss = 100;
  limits.max_portfolio_loss = 100;
  view.strategy_pnl = -99;
  EXPECT_TRUE(eval(make_order(1, 100), view, limits).approved);
  view.strategy_pnl = -100;
  EXPECT_TRUE(eval(make_order(1, 100), view, limits).approved);
  view.strategy_pnl = -101;
  Decision breach = eval(make_order(1, 100), view, limits);
  expect_reject(breach, LimitId::Rl10, 101, 100);
  EXPECT_TRUE(breach.strategy_halt_edge);

  Probe probe;
  RiskGate gate = RiskGate::with_fixture_limits(limits);
  gate.set_journal(journal_of(&probe));
  ASSERT_EQ(gate.evaluate(make_order(1, 100), view, 5'000).status, GateStatus::Rejected);
  view.strategy_pnl = 0;
  const GateResult held = gate.evaluate(make_order(1, 100), view, 6'000);
  EXPECT_EQ(held.status, GateStatus::Rejected);
  EXPECT_EQ(held.limit, LimitId::Rl10);
  gate.on_session_start();
  EXPECT_EQ(gate.evaluate(make_order(1, 100), view, 7'000).status, GateStatus::Approved);

  LimitSet portfolio_limits = wide_limits();
  portfolio_limits.max_portfolio_loss = 50;
  PortfolioView book = calm_view();
  book.portfolio_pnl = -51;
  Decision portfolio = eval(make_order(1, 100), book, portfolio_limits);
  expect_reject(portfolio, LimitId::Rl10, 51, 50);
  EXPECT_TRUE(portfolio.portfolio_halt_edge);
}

TEST(RiskGate, Rl11DrawdownFiresOnce) {
  PortfolioView view = calm_view();
  view.high_water = 1000;
  LimitSet limits = wide_limits();
  limits.max_drawdown = 100;
  view.equity = 901;
  EXPECT_TRUE(eval(make_order(1, 100), view, limits).approved);
  view.equity = 900;
  EXPECT_TRUE(eval(make_order(1, 100), view, limits).approved);
  view.equity = 899;
  Decision breach = eval(make_order(1, 100), view, limits);
  expect_reject(breach, LimitId::Rl11, 101, 100);
  EXPECT_TRUE(breach.kill_edge);

  Probe probe;
  RiskGate gate = RiskGate::with_fixture_limits(limits);
  gate.set_journal(journal_of(&probe));
  const GateResult first = gate.evaluate(make_order(1, 100), view, 10);
  EXPECT_EQ(first.status, GateStatus::Rejected);
  EXPECT_TRUE(first.kill_edge);
  const GateResult second = gate.evaluate(make_order(1, 100), view, 11);
  EXPECT_EQ(second.status, GateStatus::Rejected);
  EXPECT_FALSE(second.kill_edge);
  EXPECT_TRUE(gate.latches().global_kill);
  gate.on_session_start();
  EXPECT_TRUE(gate.latches().global_kill);
  EXPECT_EQ(gate.evaluate(make_order(1, 100), view, 12).status, GateStatus::Rejected);
}

TEST(RiskGate, Rl12SelfTrade) {
  RestingOrder resting[] = {RestingOrder{11, Side::Sell, 100}};
  PortfolioView view = calm_view();
  view.resting = resting;
  const LimitSet limits = wide_limits();
  EXPECT_TRUE(eval(make_order(1, 99), view, limits).approved);
  expect_reject(eval(make_order(1, 100), view, limits), LimitId::Rl12, 100, 100);
  expect_reject(eval(make_order(1, 101), view, limits), LimitId::Rl12, 101, 100);

  resting[0].side = Side::Buy;
  resting[0].price_ticks = 100;
  EXPECT_TRUE(eval(make_order(1, 101, Side::Sell), view, limits).approved);
  expect_reject(eval(make_order(1, 100, Side::Sell), view, limits), LimitId::Rl12, 100, 100);

  resting[0].instrument_id = 99;
  EXPECT_TRUE(eval(make_order(1, 100, Side::Sell), view, limits).approved);
  resting[0].instrument_id = 11;
  resting[0].side = Side::Buy;
  EXPECT_TRUE(eval(make_order(1, 100, Side::Buy), view, limits).approved);
}

TEST(RiskGate, Rl13Margin) {
  PortfolioView view = calm_view();
  view.margin_headroom = 100;
  view.order_margin = 40;
  LimitSet limits = wide_limits();
  limits.min_margin_headroom = 50;
  EXPECT_TRUE(eval(make_order(1, 100), view, limits).approved);
  limits.min_margin_headroom = 60;
  EXPECT_TRUE(eval(make_order(1, 100), view, limits).approved);
  limits.min_margin_headroom = 61;
  expect_reject(eval(make_order(1, 100), view, limits), LimitId::Rl13, 60, 61);

  view.margin_stale = true;
  limits.min_margin_headroom = 0;
  expect_reject(eval(make_order(1, 100), view, limits), LimitId::Rl13, 100, 0);
  Position book[] = {Position{11, 3, 10, 100}};
  view.positions = book;
  EXPECT_TRUE(eval(make_order(1, 100, Side::Sell), view, limits).approved);
}

TEST(RiskGate, Rl14ExpiryDay) {
  PortfolioView view = calm_view();
  view.in_final_session = true;
  const LimitSet limits = wide_limits();
  expect_reject(eval(make_order(1, 100), view, limits), LimitId::Rl14, 1, 0);
  Position book[] = {Position{11, 3, 5, 100}};
  view.positions = book;
  EXPECT_TRUE(eval(make_order(1, 100, Side::Sell), view, limits).approved);
  view.in_final_session = false;
  view.positions = {};
  EXPECT_TRUE(eval(make_order(1, 100), view, limits).approved);
}

TEST(RiskGate, Rl15StaleInstrument) {
  PortfolioView stale = calm_view();
  stale.instrument_stale = true;
  expect_reject(eval(make_order(1, 100), stale, wide_limits()), LimitId::Rl15, 1, 0);
  PortfolioView fresh = calm_view();
  fresh.instrument_stale = false;
  EXPECT_TRUE(eval(make_order(1, 100), fresh, wide_limits()).approved);
  Position book[] = {Position{11, 3, 4, 100}};
  stale.positions = book;
  expect_reject(eval(make_order(1, 100, Side::Sell), stale, wide_limits()), LimitId::Rl15, 1, 0);
}

TEST(RiskGate, Rl16ReduceOnly) {
  Position book[] = {Position{11, 3, 10, 100}};
  PortfolioView view = calm_view();
  view.positions = book;
  view.reduce_only = true;
  const LimitSet limits = wide_limits();
  EXPECT_TRUE(eval(make_order(9, 100, Side::Sell), view, limits).approved);
  EXPECT_TRUE(eval(make_order(10, 100, Side::Sell), view, limits).approved);
  expect_reject(eval(make_order(11, 100, Side::Sell), view, limits), LimitId::Rl16, -1, 0);
  expect_reject(eval(make_order(1, 100, Side::Buy), view, limits), LimitId::Rl16, 11, 0);
}

TEST(RiskGate, JournalAndToken) {
  Probe probe;
  RiskGate gate = RiskGate::with_fixture_limits(wide_limits());
  gate.set_journal(journal_of(&probe));
  const std::int64_t now = 1'000'000'000;
  const GateResult approved = gate.evaluate(make_order(1, 100), calm_view(), now);
  EXPECT_EQ(approved.status, GateStatus::Approved);
  EXPECT_EQ(approved.token.token_id, 1U);
  EXPECT_EQ(approved.token.limit_set_version, 1U);
  EXPECT_EQ(approved.token.expiry_mono_ns, now + kApprovalTtlNs);
  EXPECT_EQ(approved.token.client_id, client("CID-1"));
  ASSERT_EQ(probe.records.size(), 1U);
  EXPECT_TRUE(probe.records[0].approved);
  EXPECT_EQ(probe.records[0].limit, LimitId::None);
  EXPECT_EQ(probe.records[0].expiry_mono_ns, now + kApprovalTtlNs);

  LimitSet tight = wide_limits();
  tight.max_order_qty = 1;
  RiskGate rejecting = RiskGate::with_fixture_limits(tight);
  Probe reject_probe;
  rejecting.set_journal(journal_of(&reject_probe));
  const GateResult rejected = rejecting.evaluate(make_order(2, 100), calm_view(), now);
  EXPECT_EQ(rejected.status, GateStatus::Rejected);
  EXPECT_EQ(rejected.token.token_id, 0U);
  EXPECT_EQ(rejected.limit, LimitId::Rl02);
  EXPECT_EQ(rejected.observed, 2);
  EXPECT_EQ(rejected.threshold, 1);
  ASSERT_EQ(reject_probe.records.size(), 1U);
  EXPECT_FALSE(reject_probe.records[0].approved);
  EXPECT_EQ(reject_probe.records[0].limit, LimitId::Rl02);
  EXPECT_EQ(reject_probe.records[0].observed, 2);
  EXPECT_EQ(reject_probe.records[0].threshold, 1);

  EXPECT_EQ(gate.accept_token(approved.token, client("CID-1"), now + kApprovalTtlNs - 1),
            TokenCheck::Ok);
  EXPECT_EQ(gate.accept_token(approved.token, client("CID-1"), now + kApprovalTtlNs - 1),
            TokenCheck::Reused);

  const GateResult second = gate.evaluate(make_order(1, 100), calm_view(), now);
  ASSERT_EQ(second.status, GateStatus::Approved);
  ApprovalToken forged = second.token;
  forged.token_id = 99;
  EXPECT_EQ(gate.accept_token(forged, client("CID-1"), now), TokenCheck::NotIssued);
  ApprovalToken mismatched = second.token;
  mismatched.client_id = client("OTHER");
  EXPECT_EQ(gate.accept_token(mismatched, client("OTHER"), now), TokenCheck::ClientMismatch);
  EXPECT_EQ(gate.accept_token(second.token, client("CID-1"), now + kApprovalTtlNs), TokenCheck::Expired);
  EXPECT_EQ(gate.accept_token(ApprovalToken{}, client("CID-1"), now), TokenCheck::Missing);
}

TEST(RiskGate, TightenOnlyAndRestore) {
  Probe probe;
  LimitSet hard = wide_limits();
  hard.max_order_qty = 100;
  hard.min_margin_headroom = 10;
  RiskGate gate = RiskGate::with_fixture_limits(hard);
  gate.set_journal(journal_of(&probe));

  LimitSet tighter = hard;
  tighter.max_order_qty = 40;
  tighter.min_margin_headroom = 25;
  ASSERT_TRUE(can_tighten(hard, tighter));
  const GateResult before = gate.evaluate(make_order(1, 100), calm_view(), 1'000);
  ASSERT_EQ(before.status, GateStatus::Approved);
  ASSERT_TRUE(gate.try_tighten(tighter));
  EXPECT_EQ(gate.accept_token(before.token, client("CID-1"), 1'000), TokenCheck::VersionMismatch);
  EXPECT_EQ(gate.limit_set_version(), 2U);
  EXPECT_EQ(gate.effective_limits().max_order_qty, 40);
  ASSERT_FALSE(probe.records.empty());
  EXPECT_TRUE(probe.records.back().tighten);

  LimitSet looser = tighter;
  looser.max_order_qty = 80;
  EXPECT_FALSE(can_tighten(gate.effective_limits(), looser));
  EXPECT_FALSE(gate.try_tighten(looser));
  EXPECT_EQ(gate.limit_set_version(), 2U);

  LimitSet thinner_buffer = tighter;
  thinner_buffer.min_margin_headroom = 11;
  EXPECT_FALSE(gate.try_tighten(thinner_buffer));

  RiskGate restored = RiskGate::with_fixture_limits(hard);
  EXPECT_TRUE(restored.restore_tightened(tighter, 2));
  EXPECT_EQ(restored.effective_limits().max_order_qty, 40);
  EXPECT_EQ(restored.limit_set_version(), 2U);
  LimitSet above_hard = hard;
  above_hard.max_order_qty = 101;
  EXPECT_FALSE(restored.restore_tightened(above_hard, 3));
  EXPECT_EQ(restored.effective_limits().max_order_qty, 40);

  probe.accept = false;
  LimitSet again = tighter;
  again.max_order_qty = 30;
  EXPECT_FALSE(gate.try_tighten(again));
  EXPECT_EQ(gate.effective_limits().max_order_qty, 40);
  EXPECT_EQ(gate.limit_set_version(), 2U);
}

TEST(RiskGate, JournalFailureDoesNotIssueOrLatch) {
  Probe probe;
  probe.accept = false;
  RiskGate gate = RiskGate::with_fixture_limits(wide_limits());
  gate.set_journal(journal_of(&probe));
  PortfolioView view = calm_view();
  view.strategy_pnl = -1'000'000'000'001;
  const GateResult failed = gate.evaluate(make_order(1, 100), view, 50);
  EXPECT_EQ(failed.status, GateStatus::JournalFailed);
  EXPECT_EQ(failed.token.token_id, 0U);
  EXPECT_TRUE(gate.latches().strategy_loss_halted.empty());

  probe.accept = true;
  view.strategy_pnl = 0;
  EXPECT_EQ(gate.evaluate(make_order(1, 100), view, 60).status, GateStatus::Approved);
}

TEST(RiskGate, DefaultGateRejectsTighten) {
  RiskGate gate;
  EXPECT_FALSE(gate.try_tighten(wide_limits()));
  EXPECT_FALSE(gate.restore_tightened(wide_limits(), 1));
}
