#include "trading/portfolio/reconciler.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using trading::journal::v1::ReconciliationOutcome;
using trading::journal::v1::ReconciliationScope;
using trading::orders::ClientOrderId;
using trading::orders::Side;
using trading::portfolio::FillRow;
using trading::portfolio::JournalPort;
using trading::portfolio::OpenOrderRow;
using trading::portfolio::PortfolioSnapshot;
using trading::portfolio::PositionRow;
using trading::portfolio::Reconciler;
using trading::portfolio::ReconcileDetail;
using trading::portfolio::SnapshotPorts;
using trading::portfolio::AlertPort;
using trading::portfolio::compare_snapshots;
using trading::portfolio::kAlertAl03;
using trading::portfolio::kPeriodicReconcileIntervalNs;

struct Harness {
  PortfolioSnapshot internal{};
  PortfolioSnapshot broker{};
  bool broker_ok{true};
  std::vector<trading::journal::v1::PositionSnapshotPayload> position_journals{};
  std::vector<trading::journal::v1::ReconciliationResultPayload> reconcile_journals{};
  std::vector<std::uint16_t> alert_codes{};
  std::vector<std::string> alert_text{};

  static void load_internal(void* ctx, PortfolioSnapshot& out) {
    auto* self = static_cast<Harness*>(ctx);
    out = self->internal;
  }

  static bool fetch_broker(void* ctx, PortfolioSnapshot& out) {
    auto* self = static_cast<Harness*>(ctx);
    if (!self->broker_ok) {
      return false;
    }
    out = self->broker;
    return true;
  }

  static bool journal_position(void* ctx, const trading::journal::v1::PositionSnapshotPayload& payload) {
    auto* self = static_cast<Harness*>(ctx);
    self->position_journals.push_back(payload);
    return true;
  }

  static bool journal_reconcile(void* ctx,
                               const trading::journal::v1::ReconciliationResultPayload& payload) {
    auto* self = static_cast<Harness*>(ctx);
    self->reconcile_journals.push_back(payload);
    return true;
  }

  static void raise_alert(void* ctx, std::uint16_t code, std::string_view detail) {
    auto* self = static_cast<Harness*>(ctx);
    self->alert_codes.push_back(code);
    self->alert_text.emplace_back(detail);
  }

  [[nodiscard]] Reconciler make_reconciler() {
    Reconciler reconciler{};
    SnapshotPorts snapshots{};
    snapshots.ctx = this;
    snapshots.load_internal = load_internal;
    snapshots.fetch_broker = fetch_broker;

    JournalPort journal{};
    journal.ctx = this;
    journal.position_snapshot = journal_position;
    journal.reconciliation_result = journal_reconcile;

    AlertPort alerts{};
    alerts.ctx = this;
    alerts.raise = raise_alert;

    reconciler.set_ports(snapshots, journal, alerts);
    return reconciler;
  }
};

ClientOrderId client(std::string_view text) {
  ClientOrderId id{};
  const std::size_t count = text.size() < id.bytes.size() ? text.size() : id.bytes.size();
  for (std::size_t i = 0; i < count; ++i) {
    id.bytes[i] = text[i];
  }
  return id;
}

PortfolioSnapshot sample_clean() {
  PortfolioSnapshot snap{};
  snap.positions.push_back(
      PositionRow{.instrument_id = 42, .net_qty_lots = 3, .avg_price_ticks = 100, .realized_pnl_ticks = 5});
  snap.open_orders.push_back(OpenOrderRow{.client_id = client("TST1-0000000001"),
                                          .instrument_id = 42,
                                          .side = Side::Buy,
                                          .qty_lots = 2,
                                          .price_ticks = 101});
  snap.fills.push_back(FillRow{.fill_id = 7, .instrument_id = 42, .qty_lots = 1, .price_ticks = 99});
  return snap;
}

TEST(ReconcilerCompare, CleanWhenSnapshotsMatch) {
  const PortfolioSnapshot a = sample_clean();
  const PortfolioSnapshot b = sample_clean();
  const auto run = compare_snapshots(a, b);
  EXPECT_EQ(run.outcome, ReconciliationOutcome::Clean);
}

TEST(Reconciler, NoTradingUntilStartupReconcileClean) {
  Harness harness{};
  harness.internal = sample_clean();
  harness.broker = sample_clean();
  Reconciler reconciler = harness.make_reconciler();
  EXPECT_FALSE(reconciler.trading_allowed());
}

TEST(Reconciler, StartupCleanMatchEnablesTradingAndSnapshots) {
  Harness harness{};
  harness.internal = sample_clean();
  harness.broker = sample_clean();
  Reconciler reconciler = harness.make_reconciler();

  const auto run = reconciler.on_connect(1'000);
  EXPECT_EQ(run.outcome, ReconciliationOutcome::Clean);
  EXPECT_TRUE(reconciler.startup_reconcile_clean());
  EXPECT_TRUE(reconciler.trading_allowed(42, 0));
  ASSERT_EQ(harness.reconcile_journals.size(), 1u);
  EXPECT_EQ(harness.reconcile_journals[0].outcome, ReconciliationOutcome::Clean);
  ASSERT_EQ(harness.position_journals.size(), 1u);
  EXPECT_EQ(harness.position_journals[0].instrument_id, 42u);
  EXPECT_EQ(harness.position_journals[0].net_qty_lots, 3);
}

TEST(Reconciler, PositionMismatchHaltsInstrumentAndRaisesAl03) {
  Harness harness{};
  harness.internal = sample_clean();
  harness.broker = sample_clean();
  harness.broker.positions[0].net_qty_lots = 4;
  Reconciler reconciler = harness.make_reconciler();

  const auto run = reconciler.on_connect(2'000);
  EXPECT_EQ(run.outcome, ReconciliationOutcome::Mismatch);
  EXPECT_EQ(run.detail, ReconcileDetail::PositionQty);
  EXPECT_EQ(run.scope, ReconciliationScope::Instrument);
  EXPECT_EQ(run.scope_id, 42u);
  EXPECT_FALSE(reconciler.trading_allowed(42, 0));
  EXPECT_FALSE(reconciler.startup_reconcile_clean());
  ASSERT_EQ(harness.alert_codes.size(), 1u);
  EXPECT_EQ(harness.alert_codes[0], kAlertAl03);
}

TEST(Reconciler, FillMismatchRecordsScope) {
  Harness harness{};
  harness.internal = sample_clean();
  harness.broker = sample_clean();
  harness.broker.fills[0].qty_lots = 2;
  Reconciler reconciler = harness.make_reconciler();
  const auto run = reconciler.on_reconnect(3'000);
  EXPECT_EQ(run.outcome, ReconciliationOutcome::Mismatch);
  EXPECT_EQ(run.detail, ReconcileDetail::Fill);
}

TEST(Reconciler, RecoveryAfterOperatorClearAndCleanRun) {
  Harness harness{};
  harness.internal = sample_clean();
  harness.broker = sample_clean();
  harness.broker.open_orders[0].qty_lots = 5;
  Reconciler reconciler = harness.make_reconciler();

  const auto mismatch = reconciler.on_connect(4'000);
  EXPECT_EQ(mismatch.outcome, ReconciliationOutcome::Mismatch);
  EXPECT_FALSE(reconciler.trading_allowed(42, 0));

  reconciler.operator_clear(ReconciliationScope::Instrument, 42);
  EXPECT_FALSE(reconciler.trading_allowed(42, 0));

  harness.broker = sample_clean();
  const auto clean = reconciler.on_reconnect(5'000);
  EXPECT_EQ(clean.outcome, ReconciliationOutcome::Clean);
  EXPECT_TRUE(reconciler.trading_allowed(42, 0));
}

TEST(Reconciler, PeriodicIntervalRespected) {
  Harness harness{};
  harness.internal = sample_clean();
  harness.broker = sample_clean();
  Reconciler reconciler = harness.make_reconciler();
  ASSERT_TRUE(reconciler.on_connect(10'000).outcome == ReconciliationOutcome::Clean);

  const std::size_t journals_before = harness.reconcile_journals.size();
  (void)reconciler.maybe_periodic(10'000 + kPeriodicReconcileIntervalNs - 1);
  EXPECT_EQ(harness.reconcile_journals.size(), journals_before);

  const auto periodic = reconciler.maybe_periodic(10'000 + kPeriodicReconcileIntervalNs);
  EXPECT_EQ(periodic.outcome, ReconciliationOutcome::Clean);
  EXPECT_GT(harness.reconcile_journals.size(), journals_before);
  EXPECT_GE(harness.position_journals.size(), 2u);
}

TEST(Reconciler, BrokerFetchFailureAbortsWithoutTrading) {
  Harness harness{};
  harness.internal = sample_clean();
  harness.broker_ok = false;
  Reconciler reconciler = harness.make_reconciler();
  const auto run = reconciler.on_connect(6'000);
  EXPECT_EQ(run.outcome, ReconciliationOutcome::Aborted);
  EXPECT_FALSE(reconciler.startup_reconcile_clean());
  EXPECT_FALSE(reconciler.trading_allowed());
}

} // namespace
