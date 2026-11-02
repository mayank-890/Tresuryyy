#include "trading/risk/gate.hpp"

#include <cstdint>
#include <limits>

namespace trading::risk {

namespace {

using strategy::Side;

[[nodiscard]] bool client_id_empty(const std::array<char, kClientIdBytes>& id) {
  for (const char byte : id) {
    if (byte != '\0') {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool client_id_equal(const std::array<char, kClientIdBytes>& lhs,
                                   const std::array<char, kClientIdBytes>& rhs) {
  for (std::size_t i = 0; i < kClientIdBytes; ++i) {
    if (lhs[i] != rhs[i]) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool max_field_ok(std::int64_t hard, std::int64_t requested) {
  return requested >= 0 && requested <= hard;
}

[[nodiscard]] Decision reject_limit(LimitId limit, std::int64_t observed, std::int64_t threshold) {
  Decision decision;
  decision.approved = false;
  decision.limit = limit;
  decision.observed = observed;
  decision.threshold = threshold;
  return decision;
}

[[nodiscard]] Decision invalid_input(std::int64_t observed) {
  Decision decision;
  decision.approved = false;
  decision.invalid_input = true;
  decision.observed = observed;
  return decision;
}

[[nodiscard]] Decision approve() {
  Decision decision;
  decision.approved = true;
  return decision;
}

[[nodiscard]] bool abs_i64(std::int64_t value, std::int64_t* out) {
  if (value == std::numeric_limits<std::int64_t>::min()) {
    return false;
  }
  *out = value < 0 ? -value : value;
  return true;
}

[[nodiscard]] std::int64_t signed_qty(const strategy::OrderIntent& intent) {
  const std::int64_t magnitude = intent.qty_lots;
  return intent.side == Side::Sell ? -magnitude : magnitude;
}

[[nodiscard]] std::int64_t net_of(const PortfolioView& view, std::uint32_t instrument_id) {
  for (const Position& position : view.positions) {
    if (position.instrument_id == instrument_id) {
      return position.net_qty_lots;
    }
  }
  return 0;
}

[[nodiscard]] std::int64_t mark_of(const PortfolioView& view, std::uint32_t instrument_id) {
  for (const Position& position : view.positions) {
    if (position.instrument_id == instrument_id) {
      return position.mark_price_ticks;
    }
  }
  return view.reference_price_ticks;
}

[[nodiscard]] bool strategy_is_halted(const LatchState& latches, std::uint32_t strategy_id) {
  if (latches.portfolio_loss_halted) {
    return true;
  }
  for (const std::uint32_t halted : latches.strategy_loss_halted) {
    if (halted == strategy_id) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool loss_magnitude(std::int64_t pnl, std::int64_t* out) {
  if (pnl >= 0) {
    *out = 0;
    return true;
  }
  return abs_i64(pnl, out);
}

[[nodiscard]] bool drawdown_of(const PortfolioView& view, std::int64_t* out) {
  if (view.equity >= view.high_water) {
    *out = 0;
    return true;
  }
  return !__builtin_sub_overflow(view.high_water, view.equity, out);
}

[[nodiscard]] std::int64_t cancel_ratio_bps(std::int32_t cancels, std::int32_t orders,
                                            std::int64_t threshold) {
  if (cancels <= 0) {
    return 0;
  }
  if (orders <= 0) {
    if (threshold == std::numeric_limits<std::int64_t>::max()) {
      return threshold;
    }
    return threshold + 1;
  }
  std::int64_t numerator = 0;
  if (__builtin_mul_overflow(static_cast<std::int64_t>(cancels), static_cast<std::int64_t>(10000),
                             &numerator)) {
    return std::numeric_limits<std::int64_t>::max();
  }
  return numerator / orders;
}

[[nodiscard]] bool leg_exposure(std::int64_t net, std::int64_t mark, std::int64_t* out) {
  std::int64_t abs_net = 0;
  std::int64_t abs_mark = 0;
  if (!abs_i64(net, &abs_net) || !abs_i64(mark, &abs_mark)) {
    return false;
  }
  return !__builtin_mul_overflow(abs_net, abs_mark, out);
}

[[nodiscard]] bool post_greek(std::int64_t current, std::int64_t per_lot, std::int64_t qty,
                              std::int64_t* out) {
  std::int64_t contribution = 0;
  if (__builtin_mul_overflow(per_lot, qty, &contribution)) {
    return false;
  }
  return !__builtin_add_overflow(current, contribution, out);
}

[[nodiscard]] bool greek_breach(std::int64_t current, std::int64_t post, std::int64_t limit,
                                LimitId limit_id, Decision* decision) {
  std::int64_t abs_post = 0;
  std::int64_t abs_current = 0;
  if (!abs_i64(post, &abs_post) || !abs_i64(current, &abs_current)) {
    *decision = reject_limit(limit_id, std::numeric_limits<std::int64_t>::max(), limit);
    return true;
  }
  if (abs_post > limit && abs_post > abs_current) {
    *decision = reject_limit(limit_id, abs_post, limit);
    return true;
  }
  return false;
}

[[nodiscard]] Decision decide_order(const GateRequest& request, const PortfolioView& view,
                                    const LimitSet& limits, const LatchState& latches) {
  const strategy::OrderIntent& intent = request.intent;
  if (intent.qty_lots <= 0 || (intent.side != Side::Buy && intent.side != Side::Sell)) {
    return invalid_input(intent.qty_lots);
  }

  std::int64_t drawdown = 0;
  if (!drawdown_of(view, &drawdown)) {
    return reject_limit(LimitId::Rl11, std::numeric_limits<std::int64_t>::max(), limits.max_drawdown);
  }
  if (latches.global_kill) {
    return reject_limit(LimitId::Rl11, drawdown, limits.max_drawdown);
  }
  if (strategy_is_halted(latches, intent.strategy_id)) {
    std::int64_t current_loss = 0;
    if (!loss_magnitude(view.strategy_pnl, &current_loss)) {
      current_loss = std::numeric_limits<std::int64_t>::max();
    }
    return reject_limit(LimitId::Rl10, current_loss, limits.max_strategy_loss);
  }
  if (view.instrument_stale) {
    return reject_limit(LimitId::Rl15, 1, 0);
  }

  const std::int64_t current_net = net_of(view, intent.instrument_id);
  std::int64_t post_net = 0;
  if (__builtin_add_overflow(current_net, signed_qty(intent), &post_net)) {
    return reject_limit(LimitId::Rl03, std::numeric_limits<std::int64_t>::max(),
                        limits.max_net_instrument);
  }
  std::int64_t abs_post = 0;
  std::int64_t abs_current = 0;
  if (!abs_i64(post_net, &abs_post) || !abs_i64(current_net, &abs_current)) {
    return reject_limit(LimitId::Rl03, std::numeric_limits<std::int64_t>::max(),
                        limits.max_net_instrument);
  }
  const bool strict_reduction = abs_post < abs_current;
  const bool crosses_zero = (current_net > 0 && post_net < 0) || (current_net < 0 && post_net > 0);

  if (view.in_final_session && !strict_reduction) {
    return reject_limit(LimitId::Rl14, 1, 0);
  }
  if (view.reduce_only && (crosses_zero || !strict_reduction)) {
    return reject_limit(LimitId::Rl16, post_net, 0);
  }

  if (request.kind == RequestKind::Order) {
    for (const RestingOrder& resting : view.resting) {
      if (resting.instrument_id != intent.instrument_id) {
        continue;
      }
      const bool buy_cross = intent.side == Side::Buy && resting.side == Side::Sell &&
                             intent.price_ticks >= resting.price_ticks;
      const bool sell_cross = intent.side == Side::Sell && resting.side == Side::Buy &&
                              intent.price_ticks <= resting.price_ticks;
      if (buy_cross || sell_cross) {
        return reject_limit(LimitId::Rl12, intent.price_ticks, resting.price_ticks);
      }
    }
  }

  std::int64_t price_abs = 0;
  std::int64_t notional = 0;
  if (!abs_i64(intent.price_ticks, &price_abs) ||
      __builtin_mul_overflow(price_abs, static_cast<std::int64_t>(intent.qty_lots), &notional)) {
    return reject_limit(LimitId::Rl01, std::numeric_limits<std::int64_t>::max(),
                        limits.max_order_notional);
  }
  if (notional > limits.max_order_notional) {
    return reject_limit(LimitId::Rl01, notional, limits.max_order_notional);
  }
  if (static_cast<std::int64_t>(intent.qty_lots) > limits.max_order_qty) {
    return reject_limit(LimitId::Rl02, intent.qty_lots, limits.max_order_qty);
  }

  if (view.reference_stale || view.reference_price_ticks == 0) {
    return reject_limit(LimitId::Rl07, intent.price_ticks, limits.collar_ticks);
  }
  std::int64_t distance = 0;
  if (__builtin_sub_overflow(intent.price_ticks, view.reference_price_ticks, &distance) ||
      !abs_i64(distance, &distance)) {
    return reject_limit(LimitId::Rl07, std::numeric_limits<std::int64_t>::max(), limits.collar_ticks);
  }
  if (distance > limits.collar_ticks) {
    return reject_limit(LimitId::Rl07, distance, limits.collar_ticks);
  }

  if (abs_post > limits.max_net_instrument && abs_post > abs_current) {
    return reject_limit(LimitId::Rl03, abs_post, limits.max_net_instrument);
  }

  std::int64_t underlying_post = 0;
  bool saw_instrument = false;
  bool underlying_overflow = false;
  for (const Position& position : view.positions) {
    if (position.underlying_id != request.underlying_id) {
      continue;
    }
    const std::int64_t contribution =
        position.instrument_id == intent.instrument_id ? post_net : position.net_qty_lots;
    if (position.instrument_id == intent.instrument_id) {
      saw_instrument = true;
    }
    if (__builtin_add_overflow(underlying_post, contribution, &underlying_post)) {
      underlying_overflow = true;
      break;
    }
  }
  if (!saw_instrument && __builtin_add_overflow(underlying_post, post_net, &underlying_post)) {
    underlying_overflow = true;
  }
  std::int64_t underlying_current = 0;
  for (const Position& position : view.positions) {
    if (position.underlying_id != request.underlying_id) {
      continue;
    }
    if (__builtin_add_overflow(underlying_current, position.net_qty_lots, &underlying_current)) {
      underlying_overflow = true;
      break;
    }
  }
  if (!underlying_overflow) {
    std::int64_t abs_under_post = 0;
    std::int64_t abs_under_current = 0;
    if (!abs_i64(underlying_post, &abs_under_post) || !abs_i64(underlying_current, &abs_under_current)) {
      underlying_overflow = true;
    } else if (abs_under_post > limits.max_net_underlying && abs_under_post > abs_under_current) {
      return reject_limit(LimitId::Rl04, abs_under_post, limits.max_net_underlying);
    }
  }
  if (underlying_overflow) {
    return reject_limit(LimitId::Rl04, std::numeric_limits<std::int64_t>::max(),
                        limits.max_net_underlying);
  }

  bool instrument_known = false;
  for (const Position& position : view.positions) {
    if (position.instrument_id == intent.instrument_id) {
      instrument_known = true;
      break;
    }
  }
  std::int64_t gross_current = 0;
  std::int64_t gross_post = 0;
  bool gross_overflow = false;
  for (const Position& position : view.positions) {
    std::int64_t current_leg = 0;
    if (!leg_exposure(position.net_qty_lots, position.mark_price_ticks, &current_leg) ||
        __builtin_add_overflow(gross_current, current_leg, &gross_current)) {
      gross_overflow = true;
      break;
    }
    const std::int64_t post_qty =
        position.instrument_id == intent.instrument_id ? post_net : position.net_qty_lots;
    std::int64_t post_leg = 0;
    if (!leg_exposure(post_qty, position.mark_price_ticks, &post_leg) ||
        __builtin_add_overflow(gross_post, post_leg, &gross_post)) {
      gross_overflow = true;
      break;
    }
  }
  if (!gross_overflow && !instrument_known) {
    std::int64_t new_leg = 0;
    if (!leg_exposure(post_net, view.reference_price_ticks, &new_leg) ||
        __builtin_add_overflow(gross_post, new_leg, &gross_post)) {
      gross_overflow = true;
    }
  }
  if (gross_overflow) {
    return reject_limit(LimitId::Rl05, std::numeric_limits<std::int64_t>::max(),
                        limits.max_gross_exposure);
  }
  if (gross_post > limits.max_gross_exposure && gross_post > gross_current) {
    return reject_limit(LimitId::Rl05, gross_post, limits.max_gross_exposure);
  }

  if (!view.traded_greeks_present) {
    return reject_limit(LimitId::Rl06, 0, 0);
  }
  std::int64_t post_delta = 0;
  std::int64_t post_gamma = 0;
  std::int64_t post_vega = 0;
  if (!post_greek(view.net_delta, view.traded_delta_per_lot, signed_qty(intent), &post_delta) ||
      !post_greek(view.net_gamma, view.traded_gamma_per_lot, signed_qty(intent), &post_gamma) ||
      !post_greek(view.net_vega, view.traded_vega_per_lot, signed_qty(intent), &post_vega)) {
    return reject_limit(LimitId::Rl06, std::numeric_limits<std::int64_t>::max(), limits.max_abs_delta);
  }
  Decision greek_reject;
  if (greek_breach(view.net_delta, post_delta, limits.max_abs_delta, LimitId::Rl06, &greek_reject) ||
      greek_breach(view.net_gamma, post_gamma, limits.max_abs_gamma, LimitId::Rl06, &greek_reject) ||
      greek_breach(view.net_vega, post_vega, limits.max_abs_vega, LimitId::Rl06, &greek_reject)) {
    return greek_reject;
  }

  if (!strict_reduction) {
    if (view.margin_stale) {
      return reject_limit(LimitId::Rl13, view.margin_headroom, limits.min_margin_headroom);
    }
    std::int64_t post_margin = 0;
    if (__builtin_sub_overflow(view.margin_headroom, view.order_margin, &post_margin)) {
      return reject_limit(LimitId::Rl13, std::numeric_limits<std::int64_t>::min(),
                          limits.min_margin_headroom);
    }
    if (post_margin < limits.min_margin_headroom) {
      return reject_limit(LimitId::Rl13, post_margin, limits.min_margin_headroom);
    }
  }

  if (!strict_reduction) {
    const std::int64_t next_entries = static_cast<std::int64_t>(view.entries_in_window) + 1;
    if (next_entries > limits.max_entries_per_window) {
      return reject_limit(LimitId::Rl08, next_entries, limits.max_entries_per_window);
    }
  }

  const std::int64_t ratio_bps =
      cancel_ratio_bps(view.cancels_in_window, view.orders_in_window, limits.max_cancel_ratio_bps);
  if (ratio_bps > limits.max_cancel_ratio_bps) {
    return reject_limit(LimitId::Rl09, ratio_bps, limits.max_cancel_ratio_bps);
  }

  std::int64_t strategy_loss = 0;
  std::int64_t portfolio_loss = 0;
  if (!loss_magnitude(view.strategy_pnl, &strategy_loss) ||
      !loss_magnitude(view.portfolio_pnl, &portfolio_loss)) {
    Decision decision = reject_limit(LimitId::Rl10, std::numeric_limits<std::int64_t>::max(),
                                     limits.max_strategy_loss);
    decision.strategy_halt_edge = true;
    decision.portfolio_halt_edge = true;
    return decision;
  }
  const bool strategy_breach = strategy_loss > limits.max_strategy_loss;
  const bool portfolio_breach = portfolio_loss > limits.max_portfolio_loss;
  if (strategy_breach || portfolio_breach) {
    Decision decision = strategy_breach
                            ? reject_limit(LimitId::Rl10, strategy_loss, limits.max_strategy_loss)
                            : reject_limit(LimitId::Rl10, portfolio_loss, limits.max_portfolio_loss);
    decision.strategy_halt_edge = strategy_breach;
    decision.portfolio_halt_edge = portfolio_breach;
    return decision;
  }

  if (drawdown > limits.max_drawdown) {
    Decision decision = reject_limit(LimitId::Rl11, drawdown, limits.max_drawdown);
    decision.kill_edge = true;
    return decision;
  }
  return approve();
}

} // namespace

bool can_tighten(const LimitSet& hard, const LimitSet& requested) {
  return max_field_ok(hard.max_order_notional, requested.max_order_notional) &&
         max_field_ok(hard.max_order_qty, requested.max_order_qty) &&
         max_field_ok(hard.max_net_instrument, requested.max_net_instrument) &&
         max_field_ok(hard.max_net_underlying, requested.max_net_underlying) &&
         max_field_ok(hard.max_gross_exposure, requested.max_gross_exposure) &&
         max_field_ok(hard.max_abs_delta, requested.max_abs_delta) &&
         max_field_ok(hard.max_abs_gamma, requested.max_abs_gamma) &&
         max_field_ok(hard.max_abs_vega, requested.max_abs_vega) &&
         max_field_ok(hard.collar_ticks, requested.collar_ticks) &&
         max_field_ok(hard.max_entries_per_window, requested.max_entries_per_window) &&
         max_field_ok(hard.max_cancel_ratio_bps, requested.max_cancel_ratio_bps) &&
         max_field_ok(hard.max_strategy_loss, requested.max_strategy_loss) &&
         max_field_ok(hard.max_portfolio_loss, requested.max_portfolio_loss) &&
         max_field_ok(hard.max_drawdown, requested.max_drawdown) &&
         requested.min_margin_headroom >= hard.min_margin_headroom;
}

Decision decide(const GateRequest& request, const PortfolioView& view, const LimitSet& limits,
                const LatchState& latches) {
  if (client_id_empty(request.client_id)) {
    return invalid_input(0);
  }
  if (request.kind == RequestKind::Cancel) {
    return approve();
  }
  return decide_order(request, view, limits, latches);
}

RiskGate::RiskGate() = default;

RiskGate RiskGate::with_fixture_limits(LimitSet hard) {
  RiskGate gate;
  gate.configured_ = true;
  gate.hard_ = hard;
  gate.effective_ = hard;
  return gate;
}

void RiskGate::set_journal(JournalPort journal) { journal_ = journal; }

bool RiskGate::journal_record(const DecisionRecord& record) const {
  return journal_.record != nullptr && journal_.record(journal_.ctx, record);
}

GateResult RiskGate::apply_decision(const GateRequest& request, Decision decision,
                                    std::int64_t now_ns) {
  DecisionRecord record;
  record.approved = decision.approved && !decision.invalid_input;
  record.client_id = request.client_id;
  record.limit = decision.limit;
  record.observed = decision.observed;
  record.threshold = decision.threshold;
  record.effective = effective_;
  if (record.approved) {
    record.limit_set_version = version_;
    if (__builtin_add_overflow(now_ns, kApprovalTtlNs, &record.expiry_mono_ns)) {
      record.approved = false;
      decision.approved = false;
      decision.invalid_input = true;
    }
  }
  if (!journal_record(record)) {
    GateResult failed;
    failed.status = GateStatus::JournalFailed;
    failed.limit = decision.limit;
    failed.observed = decision.observed;
    failed.threshold = decision.threshold;
    return failed;
  }
  if (decision.strategy_halt_edge) {
    const std::uint32_t strategy_id = request.intent.strategy_id;
    bool already = false;
    for (const std::uint32_t halted : latches_.strategy_loss_halted) {
      if (halted == strategy_id) {
        already = true;
        break;
      }
    }
    if (!already) {
      latches_.strategy_loss_halted.push_back(strategy_id);
    }
  }
  if (decision.portfolio_halt_edge) {
    latches_.portfolio_loss_halted = true;
  }
  if (decision.kill_edge) {
    latches_.global_kill = true;
    latches_.kill_announced = true;
  }

  GateResult result;
  result.limit = decision.limit;
  result.observed = decision.observed;
  result.threshold = decision.threshold;
  result.kill_edge = decision.kill_edge;
  if (decision.invalid_input) {
    result.status = GateStatus::InvalidInput;
    return result;
  }
  if (!decision.approved) {
    result.status = GateStatus::Rejected;
    return result;
  }

  Issued issued;
  issued.token.client_id = request.client_id;
  issued.token.limit_set_version = version_;
  issued.token.expiry_mono_ns = record.expiry_mono_ns;
  issued.token.token_id = next_token_id_;
  ++next_token_id_;
  result.status = GateStatus::Approved;
  result.token = issued.token;
  issued_.push_back(issued);
  return result;
}

GateResult RiskGate::evaluate(const GateRequest& request, const PortfolioView& view,
                              std::int64_t now_ns) {
  if (!configured_) {
    DecisionRecord record;
    record.approved = false;
    record.client_id = request.client_id;
    record.limit = LimitId::None;
    if (!journal_record(record)) {
      GateResult failed;
      failed.status = GateStatus::JournalFailed;
      return failed;
    }
    GateResult result;
    result.status = GateStatus::NotConfigured;
    return result;
  }
  return apply_decision(request, decide(request, view, effective_, latches_), now_ns);
}

bool RiskGate::try_tighten(const LimitSet& requested) {
  if (!configured_ || !can_tighten(hard_, requested) || !can_tighten(effective_, requested)) {
    return false;
  }
  const LimitSet previous = effective_;
  const std::uint32_t previous_version = version_;
  effective_ = requested;
  ++version_;
  DecisionRecord record;
  record.approved = true;
  record.tighten = true;
  record.limit_set_version = version_;
  record.effective = effective_;
  if (!journal_record(record)) {
    effective_ = previous;
    version_ = previous_version;
    return false;
  }
  return true;
}

bool RiskGate::restore_tightened(const LimitSet& tightened, std::uint32_t version) {
  if (!configured_ || !can_tighten(hard_, tightened)) {
    return false;
  }
  effective_ = tightened;
  version_ = version;
  return true;
}

void RiskGate::on_session_start() {
  latches_.portfolio_loss_halted = false;
  latches_.strategy_loss_halted.clear();
}

TokenCheck RiskGate::accept_token(const ApprovalToken& token,
                                  const std::array<char, kClientIdBytes>& client_id,
                                  std::int64_t now_ns) {
  if (token.token_id == 0) {
    return TokenCheck::Missing;
  }
  Issued* found = nullptr;
  for (Issued& issued : issued_) {
    if (issued.token.token_id == token.token_id) {
      found = &issued;
      break;
    }
  }
  if (found == nullptr) {
    return TokenCheck::NotIssued;
  }
  if (found->consumed) {
    return TokenCheck::Reused;
  }
  if (!client_id_equal(token.client_id, client_id) ||
      !client_id_equal(found->token.client_id, client_id)) {
    return TokenCheck::ClientMismatch;
  }
  if (token.limit_set_version != found->token.limit_set_version ||
      token.expiry_mono_ns != found->token.expiry_mono_ns ||
      token.limit_set_version != version_) {
    return TokenCheck::VersionMismatch;
  }
  if (now_ns >= token.expiry_mono_ns) {
    return TokenCheck::Expired;
  }
  found->consumed = true;
  return TokenCheck::Ok;
}

} // namespace trading::risk
