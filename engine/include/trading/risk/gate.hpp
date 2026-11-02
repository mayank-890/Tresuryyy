#pragma once

#include "trading/strategy/intent.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace trading::risk {

// DESIGN §6.1. This is the approval lifetime, not an RL ceiling.
inline constexpr std::int64_t kApprovalTtlNs = 500'000'000;
inline constexpr std::size_t kClientIdBytes = 24;

enum class LimitId : std::uint16_t {
  None = 0,
  Rl01 = 1,
  Rl02 = 2,
  Rl03 = 3,
  Rl04 = 4,
  Rl05 = 5,
  Rl06 = 6,
  Rl07 = 7,
  Rl08 = 8,
  Rl09 = 9,
  Rl10 = 10,
  Rl11 = 11,
  Rl12 = 12,
  Rl13 = 13,
  Rl14 = 14,
  Rl15 = 15,
  Rl16 = 16,
};

enum class RequestKind : std::uint8_t { Order = 1, Cancel = 2 };

enum class GateStatus : std::uint8_t {
  Approved = 1,
  Rejected = 2,
  InvalidInput = 3,
  NotConfigured = 4,
  JournalFailed = 5,
};

enum class TokenCheck : std::uint8_t {
  Ok = 1,
  Missing = 2,
  Expired = 3,
  ClientMismatch = 4,
  Reused = 5,
  VersionMismatch = 6,
  NotIssued = 7,
};

// Effective ceilings for one evaluation. Max fields tighten by decreasing.
// min_margin_headroom tightens by increasing. These are not operator values.
struct LimitSet {
  std::int64_t max_order_notional{0};
  std::int64_t max_order_qty{0};
  std::int64_t max_net_instrument{0};
  std::int64_t max_net_underlying{0};
  std::int64_t max_gross_exposure{0};
  std::int64_t max_abs_delta{0};
  std::int64_t max_abs_gamma{0};
  std::int64_t max_abs_vega{0};
  std::int64_t collar_ticks{0};
  std::int64_t max_entries_per_window{0};
  std::int64_t max_cancel_ratio_bps{0};
  std::int64_t max_strategy_loss{0};
  std::int64_t max_portfolio_loss{0};
  std::int64_t max_drawdown{0};
  std::int64_t min_margin_headroom{0};
};

struct Position {
  std::uint32_t instrument_id{0};
  std::uint32_t underlying_id{0};
  std::int64_t net_qty_lots{0};
  std::int64_t mark_price_ticks{0};
};

struct RestingOrder {
  std::uint32_t instrument_id{0};
  strategy::Side side{strategy::Side::Buy};
  std::int64_t price_ticks{0};
};

// Caller-owned snapshot. The gate does not mutate it.
struct PortfolioView {
  std::span<const Position> positions{};
  std::span<const RestingOrder> resting{};
  std::int64_t net_delta{0};
  std::int64_t net_gamma{0};
  std::int64_t net_vega{0};
  std::int64_t traded_delta_per_lot{0};
  std::int64_t traded_gamma_per_lot{0};
  std::int64_t traded_vega_per_lot{0};
  bool traded_greeks_present{true};
  std::int64_t margin_headroom{0};
  std::int64_t order_margin{0};
  bool margin_stale{false};
  std::int64_t reference_price_ticks{0};
  bool reference_stale{false};
  bool instrument_stale{false};
  bool in_final_session{false};
  bool reduce_only{false};
  std::int64_t strategy_pnl{0};
  std::int64_t portfolio_pnl{0};
  std::int64_t equity{0};
  std::int64_t high_water{0};
  std::int32_t entries_in_window{0};
  std::int32_t orders_in_window{0};
  std::int32_t cancels_in_window{0};
};

struct LatchState {
  bool portfolio_loss_halted{false};
  bool global_kill{false};
  bool kill_announced{false};
  std::vector<std::uint32_t> strategy_loss_halted{};
};

struct GateRequest {
  std::array<char, kClientIdBytes> client_id{};
  strategy::OrderIntent intent{};
  std::uint32_t underlying_id{0};
  RequestKind kind{RequestKind::Order};
};

struct Decision {
  bool approved{false};
  bool invalid_input{false};
  LimitId limit{LimitId::None};
  std::int64_t observed{0};
  std::int64_t threshold{0};
  bool kill_edge{false};
  bool strategy_halt_edge{false};
  bool portfolio_halt_edge{false};
};

struct ApprovalToken {
  std::array<char, kClientIdBytes> client_id{};
  std::uint32_t limit_set_version{0};
  std::int64_t expiry_mono_ns{0};
  std::uint64_t token_id{0};
};

struct GateResult {
  GateStatus status{GateStatus::Rejected};
  ApprovalToken token{};
  LimitId limit{LimitId::None};
  std::int64_t observed{0};
  std::int64_t threshold{0};
  bool kill_edge{false};
};

struct DecisionRecord {
  bool approved{false};
  bool tighten{false};
  std::array<char, kClientIdBytes> client_id{};
  LimitId limit{LimitId::None};
  std::int64_t observed{0};
  std::int64_t threshold{0};
  std::uint32_t limit_set_version{0};
  std::int64_t expiry_mono_ns{0};
  LimitSet effective{};
};

struct JournalPort {
  void* ctx{nullptr};
  bool (*record)(void* ctx, const DecisionRecord& record){nullptr};
};

// True when every max field is <= hard and min_margin_headroom is >= hard.
[[nodiscard]] bool can_tighten(const LimitSet& hard, const LimitSet& requested);

// Pure and deterministic. Same request, view, limits, and latches always
// produce the same decision. Does not journal and does not issue a token.
[[nodiscard]] Decision decide(const GateRequest& request, const PortfolioView& view,
                              const LimitSet& limits, const LatchState& latches);

// The only issuer of ApprovalToken. A default gate has no compiled ceilings
// and refuses every order. with_fixture_limits is for tests of the check
// logic; it is not a strategy entry point.
class RiskGate {
public:
  RiskGate();
  static RiskGate with_fixture_limits(LimitSet hard);

  void set_journal(JournalPort journal);

  [[nodiscard]] GateResult evaluate(const GateRequest& request, const PortfolioView& view,
                                    std::int64_t now_ns);
  [[nodiscard]] bool try_tighten(const LimitSet& requested);
  [[nodiscard]] bool restore_tightened(const LimitSet& tightened, std::uint32_t version);
  void on_session_start();

  [[nodiscard]] TokenCheck accept_token(const ApprovalToken& token,
                                        const std::array<char, kClientIdBytes>& client_id,
                                        std::int64_t now_ns);

  [[nodiscard]] bool configured() const { return configured_; }
  [[nodiscard]] const LimitSet& effective_limits() const { return effective_; }
  [[nodiscard]] std::uint32_t limit_set_version() const { return version_; }
  [[nodiscard]] const LatchState& latches() const { return latches_; }

private:
  struct Issued {
    ApprovalToken token{};
    bool consumed{false};
  };

  [[nodiscard]] bool journal_record(const DecisionRecord& record) const;
  [[nodiscard]] GateResult apply_decision(const GateRequest& request, Decision decision,
                                          std::int64_t now_ns);

  bool configured_{false};
  LimitSet hard_{};
  LimitSet effective_{};
  std::uint32_t version_{1};
  LatchState latches_{};
  JournalPort journal_{};
  std::uint64_t next_token_id_{1};
  std::vector<Issued> issued_{};
};

} // namespace trading::risk
