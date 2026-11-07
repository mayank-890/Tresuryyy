#include "trading/portfolio/reconciler.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace trading::portfolio {

namespace {

void copy_message(ReconcileRun& run, const char* text) noexcept {
  if (text == nullptr) {
    run.detail_message[0] = '\0';
    return;
  }
  std::snprintf(run.detail_message.data(), run.detail_message.size(), "%s", text);
}

void sort_snapshot(PortfolioSnapshot& snapshot) {
  std::sort(snapshot.positions.begin(), snapshot.positions.end(),
            [](const PositionRow& a, const PositionRow& b) {
              return a.instrument_id < b.instrument_id;
            });
  std::sort(snapshot.open_orders.begin(), snapshot.open_orders.end(),
            [](const OpenOrderRow& a, const OpenOrderRow& b) {
              return client_id_less(a.client_id, b.client_id);
            });
  std::sort(snapshot.fills.begin(), snapshot.fills.end(),
            [](const FillRow& a, const FillRow& b) { return a.fill_id < b.fill_id; });
}

[[nodiscard]] journal::v1::ReconciliationResultPayload to_journal_payload(const ReconcileRun& run) {
  journal::v1::ReconciliationResultPayload payload{};
  payload.scope = run.scope;
  payload.outcome = run.outcome;
  payload.detail_code = static_cast<std::uint16_t>(run.detail);
  payload.detail_message = run.detail_message.data();
  return payload;
}

} // namespace

ReconcileRun compare_snapshots(const PortfolioSnapshot& internal,
                               const PortfolioSnapshot& broker) noexcept {
  PortfolioSnapshot lhs = internal;
  PortfolioSnapshot rhs = broker;
  sort_snapshot(lhs);
  sort_snapshot(rhs);

  ReconcileRun run{};
  run.outcome = journal::v1::ReconciliationOutcome::Clean;

  if (lhs.positions.size() != rhs.positions.size()) {
    run.scope = journal::v1::ReconciliationScope::Global;
    run.outcome = journal::v1::ReconciliationOutcome::Mismatch;
    run.detail = ReconcileDetail::PositionQty;
    copy_message(run, "position row count mismatch");
    return run;
  }

  for (std::size_t i = 0; i < lhs.positions.size(); ++i) {
    const PositionRow& a = lhs.positions[i];
    const PositionRow& b = rhs.positions[i];
    if (a.instrument_id != b.instrument_id) {
      run.scope = journal::v1::ReconciliationScope::Instrument;
      run.scope_id = a.instrument_id != 0 ? a.instrument_id : b.instrument_id;
      run.outcome = journal::v1::ReconciliationOutcome::Mismatch;
      run.detail = ReconcileDetail::PositionQty;
      copy_message(run, "position instrument mismatch");
      return run;
    }
    if (a.net_qty_lots != b.net_qty_lots) {
      run.scope = journal::v1::ReconciliationScope::Instrument;
      run.scope_id = a.instrument_id;
      run.outcome = journal::v1::ReconciliationOutcome::Mismatch;
      run.detail = ReconcileDetail::PositionQty;
      copy_message(run, "net qty mismatch");
      return run;
    }
    if (a.avg_price_ticks != b.avg_price_ticks) {
      run.scope = journal::v1::ReconciliationScope::Instrument;
      run.scope_id = a.instrument_id;
      run.outcome = journal::v1::ReconciliationOutcome::Mismatch;
      run.detail = ReconcileDetail::PositionPrice;
      copy_message(run, "avg price mismatch");
      return run;
    }
    if (a.realized_pnl_ticks != b.realized_pnl_ticks) {
      run.scope = journal::v1::ReconciliationScope::Instrument;
      run.scope_id = a.instrument_id;
      run.outcome = journal::v1::ReconciliationOutcome::Mismatch;
      run.detail = ReconcileDetail::PositionPnl;
      copy_message(run, "realized pnl mismatch");
      return run;
    }
  }

  if (lhs.open_orders.size() != rhs.open_orders.size()) {
    run.scope = journal::v1::ReconciliationScope::Global;
    run.outcome = journal::v1::ReconciliationOutcome::Mismatch;
    run.detail = ReconcileDetail::OpenOrder;
    copy_message(run, "open order count mismatch");
    return run;
  }

  for (std::size_t i = 0; i < lhs.open_orders.size(); ++i) {
    const OpenOrderRow& a = lhs.open_orders[i];
    const OpenOrderRow& b = rhs.open_orders[i];
    if (!(a.client_id == b.client_id)) {
      run.scope = journal::v1::ReconciliationScope::Global;
      run.outcome = journal::v1::ReconciliationOutcome::Mismatch;
      run.detail = ReconcileDetail::OpenOrder;
      copy_message(run, "open order client id mismatch");
      return run;
    }
    if (a.instrument_id != b.instrument_id || a.qty_lots != b.qty_lots ||
        a.price_ticks != b.price_ticks || a.side != b.side) {
      run.scope = journal::v1::ReconciliationScope::Instrument;
      run.scope_id = a.instrument_id;
      run.outcome = journal::v1::ReconciliationOutcome::Mismatch;
      run.detail = ReconcileDetail::OpenOrder;
      copy_message(run, "open order field mismatch");
      return run;
    }
  }

  if (lhs.fills.size() != rhs.fills.size()) {
    run.scope = journal::v1::ReconciliationScope::Global;
    run.outcome = journal::v1::ReconciliationOutcome::Mismatch;
    run.detail = ReconcileDetail::Fill;
    copy_message(run, "fill count mismatch");
    return run;
  }

  for (std::size_t i = 0; i < lhs.fills.size(); ++i) {
    const FillRow& a = lhs.fills[i];
    const FillRow& b = rhs.fills[i];
    if (a.fill_id != b.fill_id) {
      run.scope = journal::v1::ReconciliationScope::Global;
      run.outcome = journal::v1::ReconciliationOutcome::Mismatch;
      run.detail = ReconcileDetail::Fill;
      copy_message(run, "fill id ordering mismatch");
      return run;
    }
    if (a.instrument_id != b.instrument_id || a.qty_lots != b.qty_lots ||
        a.price_ticks != b.price_ticks) {
      run.scope = journal::v1::ReconciliationScope::Instrument;
      run.scope_id = a.instrument_id;
      run.outcome = journal::v1::ReconciliationOutcome::Mismatch;
      run.detail = ReconcileDetail::Fill;
      copy_message(run, "fill field mismatch");
      return run;
    }
  }

  run.scope = journal::v1::ReconciliationScope::Global;
  return run;
}

void Reconciler::set_ports(SnapshotPorts snapshots, JournalPort journal, AlertPort alerts) {
  snapshots_ = snapshots;
  journal_ = journal;
  alerts_ = alerts;
}

ReconcileRun Reconciler::on_connect(const std::int64_t now_ns) {
  return run(ReconcileTrigger::Connect, now_ns);
}

ReconcileRun Reconciler::on_reconnect(const std::int64_t now_ns) {
  return run(ReconcileTrigger::Reconnect, now_ns);
}

ReconcileRun Reconciler::on_periodic(const std::int64_t now_ns) {
  return run(ReconcileTrigger::Periodic, now_ns);
}

ReconcileRun Reconciler::maybe_periodic(const std::int64_t now_ns) {
  if ((now_ns - last_periodic_ns_) < kPeriodicReconcileIntervalNs) {
    return last_run_;
  }
  return on_periodic(now_ns);
}

void Reconciler::operator_clear(const journal::v1::ReconciliationScope scope,
                                const std::uint32_t scope_id) {
  switch (scope) {
  case journal::v1::ReconciliationScope::Global:
    global_halted_ = false;
    instrument_halted_.fill(false);
    strategy_halted_.fill(false);
    break;
  case journal::v1::ReconciliationScope::Instrument:
    if (scope_id < instrument_halted_.size()) {
      instrument_halted_[scope_id] = false;
    }
    break;
  case journal::v1::ReconciliationScope::Strategy:
    if (scope_id < strategy_halted_.size()) {
      strategy_halted_[scope_id] = false;
    }
    break;
  }
}

bool Reconciler::instrument_halted(const std::uint32_t instrument_id) const noexcept {
  return instrument_id < instrument_halted_.size() && instrument_halted_[instrument_id];
}

bool Reconciler::strategy_halted(const std::uint32_t strategy_id) const noexcept {
  return strategy_id < strategy_halted_.size() && strategy_halted_[strategy_id];
}

bool Reconciler::trading_allowed(const std::uint32_t instrument_id,
                                 const std::uint32_t strategy_id) const noexcept {
  if (!startup_clean_ || global_halted_) {
    return false;
  }
  if (instrument_id != 0 && instrument_halted(instrument_id)) {
    return false;
  }
  if (strategy_id != 0 && strategy_halted(strategy_id)) {
    return false;
  }
  return true;
}

ReconcileRun Reconciler::run(const ReconcileTrigger trigger, const std::int64_t now_ns) {
  (void)trigger;
  (void)now_ns;

  PortfolioSnapshot internal{};
  PortfolioSnapshot broker{};
  if (snapshots_.load_internal != nullptr) {
    snapshots_.load_internal(snapshots_.ctx, internal);
  }
  if (snapshots_.fetch_broker == nullptr || !snapshots_.fetch_broker(snapshots_.ctx, broker)) {
    ReconcileRun aborted{};
    aborted.scope = journal::v1::ReconciliationScope::Global;
    aborted.outcome = journal::v1::ReconciliationOutcome::Aborted;
    aborted.detail = ReconcileDetail::BrokerFetchFailed;
    copy_message(aborted, "broker snapshot fetch failed");
    last_run_ = aborted;
    journal_reconciliation(aborted);
    return aborted;
  }

  ReconcileRun result = compare_snapshots(internal, broker);
  last_run_ = result;

  if (!journal_reconciliation(result)) {
    ReconcileRun failed = result;
    failed.outcome = journal::v1::ReconciliationOutcome::Aborted;
    failed.detail = ReconcileDetail::BrokerFetchFailed;
    copy_message(failed, "journal write failed");
    last_run_ = failed;
    return failed;
  }

  if (result.outcome == journal::v1::ReconciliationOutcome::Clean) {
    write_position_snapshots(internal);
    if (trigger == ReconcileTrigger::Connect) {
      startup_clean_ = true;
    } else if (trigger == ReconcileTrigger::Reconnect && !startup_clean_) {
      // FR-OP-02: startup connect must be clean; a later reconnect may establish readiness.
      startup_clean_ = true;
    }
  } else if (result.outcome == journal::v1::ReconciliationOutcome::Mismatch) {
    apply_halt(result);
    if (alerts_.raise != nullptr) {
      alerts_.raise(alerts_.ctx, kAlertAl03, std::string_view(result.detail_message.data()));
    }
  }

  last_periodic_ns_ = now_ns;

  return result;
}

void Reconciler::apply_halt(const ReconcileRun& run) {
  switch (run.scope) {
  case journal::v1::ReconciliationScope::Global:
    global_halted_ = true;
    break;
  case journal::v1::ReconciliationScope::Instrument:
    if (run.scope_id < instrument_halted_.size()) {
      instrument_halted_[run.scope_id] = true;
    }
    break;
  case journal::v1::ReconciliationScope::Strategy:
    if (run.scope_id < strategy_halted_.size()) {
      strategy_halted_[run.scope_id] = true;
    }
    break;
  }
}

void Reconciler::write_position_snapshots(const PortfolioSnapshot& snapshot) {
  if (journal_.position_snapshot == nullptr) {
    return;
  }
  for (const PositionRow& row : snapshot.positions) {
    journal::v1::PositionSnapshotPayload payload{};
    payload.instrument_id = row.instrument_id;
    payload.net_qty_lots = row.net_qty_lots;
    payload.avg_price_ticks = row.avg_price_ticks;
    payload.realized_pnl_ticks = row.realized_pnl_ticks;
    (void)journal_.position_snapshot(journal_.ctx, payload);
  }
}

bool Reconciler::journal_reconciliation(const ReconcileRun& run) {
  if (journal_.reconciliation_result == nullptr) {
    return true;
  }
  return journal_.reconciliation_result(journal_.ctx, to_journal_payload(run));
}

} // namespace trading::portfolio
