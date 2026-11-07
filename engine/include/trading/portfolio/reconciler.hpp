#pragma once

#include "trading/journal/generated/records.hpp"
#include "trading/portfolio/types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace trading::portfolio {

// USER_OPERATIONS.md §8 — reconciliation mismatch.
inline constexpr std::uint16_t kAlertAl03 = 3;

inline constexpr std::int64_t kPeriodicReconcileIntervalNs = 60'000'000'000;

enum class ReconcileTrigger : std::uint8_t {
  Connect = 1,
  Reconnect = 2,
  Periodic = 3,
};

enum class ReconcileDetail : std::uint16_t {
  None = 0,
  PositionQty = 1,
  PositionPrice = 2,
  PositionPnl = 3,
  OpenOrder = 4,
  Fill = 5,
  BrokerFetchFailed = 100,
};

struct ReconcileRun {
  journal::v1::ReconciliationScope scope{journal::v1::ReconciliationScope::Global};
  journal::v1::ReconciliationOutcome outcome{journal::v1::ReconciliationOutcome::Aborted};
  ReconcileDetail detail{ReconcileDetail::None};
  std::uint32_t scope_id{0};
  std::array<char, 256> detail_message{};
};

struct SnapshotPorts {
  void* ctx{nullptr};
  void (*load_internal)(void* ctx, PortfolioSnapshot& out){nullptr};
  bool (*fetch_broker)(void* ctx, PortfolioSnapshot& out){nullptr};
};

struct JournalPort {
  void* ctx{nullptr};
  bool (*position_snapshot)(void* ctx, const journal::v1::PositionSnapshotPayload& payload){nullptr};
  bool (*reconciliation_result)(void* ctx, const journal::v1::ReconciliationResultPayload& payload){nullptr};
};

struct AlertPort {
  void* ctx{nullptr};
  void (*raise)(void* ctx, std::uint16_t alert_code, std::string_view detail){nullptr};
};

// Pure comparison for tests and the reconciler run path.
[[nodiscard]] ReconcileRun compare_snapshots(const PortfolioSnapshot& internal,
                                             const PortfolioSnapshot& broker) noexcept;

// FR-PF-02, FR-PF-03, FR-OP-02. Does not call real broker REST; snapshots come from ports.
class Reconciler {
public:
  void set_ports(SnapshotPorts snapshots, JournalPort journal, AlertPort alerts);

  [[nodiscard]] ReconcileRun on_connect(std::int64_t now_ns);
  [[nodiscard]] ReconcileRun on_reconnect(std::int64_t now_ns);
  [[nodiscard]] ReconcileRun on_periodic(std::int64_t now_ns);
  [[nodiscard]] ReconcileRun maybe_periodic(std::int64_t now_ns);

  // Operator acknowledgment after reviewing a mismatch (USER_OPERATIONS §5 / RB-02).
  void operator_clear(journal::v1::ReconciliationScope scope, std::uint32_t scope_id = 0);

  [[nodiscard]] bool startup_reconcile_clean() const noexcept { return startup_clean_; }
  [[nodiscard]] bool global_halted() const noexcept { return global_halted_; }
  [[nodiscard]] bool instrument_halted(std::uint32_t instrument_id) const noexcept;
  [[nodiscard]] bool strategy_halted(std::uint32_t strategy_id) const noexcept;

  // FR-OP-02: no trading until startup reconciliation completes cleanly, and not while halted.
  [[nodiscard]] bool trading_allowed(std::uint32_t instrument_id = 0,
                                     std::uint32_t strategy_id = 0) const noexcept;

  [[nodiscard]] std::int64_t last_periodic_mono_ns() const noexcept { return last_periodic_ns_; }
  [[nodiscard]] const ReconcileRun& last_run() const noexcept { return last_run_; }

private:
  [[nodiscard]] ReconcileRun run(ReconcileTrigger trigger, std::int64_t now_ns);
  void apply_halt(const ReconcileRun& run);
  void write_position_snapshots(const PortfolioSnapshot& snapshot);
  bool journal_reconciliation(const ReconcileRun& run);

  SnapshotPorts snapshots_{};
  JournalPort journal_{};
  AlertPort alerts_{};

  bool startup_clean_{false};
  bool global_halted_{false};
  std::array<bool, 1024> instrument_halted_{};
  std::array<bool, 256> strategy_halted_{};

  std::int64_t last_periodic_ns_{0};
  ReconcileRun last_run_{};
};

} // namespace trading::portfolio
