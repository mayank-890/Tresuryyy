// Journal v1 round-trip tests (T-0004). Compiled by just test-journal-schema.

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "trading/journal/generated/records.hpp"

namespace v1 = trading::journal::v1;

// Payload equality via manual compare where operator== is not generated.
#define FIELD_EQ(a, b, field) ((a).field == (b).field)

bool operator==(const v1::SessionStartPayload& a, const v1::SessionStartPayload& b) {
  return a.profile_name == b.profile_name && a.config_hash == b.config_hash &&
         a.engine_build_hash == b.engine_build_hash && a.client_id_prefix == b.client_id_prefix;
}

bool operator==(const v1::SessionEndPayload& a, const v1::SessionEndPayload& b) {
  return a.reason == b.reason && a.final_state == b.final_state;
}

bool operator==(const v1::MarketQuotePayload& a, const v1::MarketQuotePayload& b) {
  return FIELD_EQ(a, b, instrument_id) && FIELD_EQ(a, b, bid_ticks) && FIELD_EQ(a, b, ask_ticks) &&
         FIELD_EQ(a, b, bid_size_lots) && FIELD_EQ(a, b, ask_size_lots) &&
         FIELD_EQ(a, b, exchange_time_ns);
}

bool operator==(const v1::MarketTradePayload& a, const v1::MarketTradePayload& b) {
  return FIELD_EQ(a, b, instrument_id) && FIELD_EQ(a, b, price_ticks) && FIELD_EQ(a, b, size_lots) &&
         a.side == b.side && FIELD_EQ(a, b, exchange_time_ns);
}

bool operator==(const v1::FeedStatePayload& a, const v1::FeedStatePayload& b) {
  return FIELD_EQ(a, b, instrument_id) && a.state == b.state && FIELD_EQ(a, b, reason_code);
}

bool operator==(const v1::OrderIntentPayload& a, const v1::OrderIntentPayload& b) {
  return a.client_id == b.client_id && FIELD_EQ(a, b, strategy_id) && FIELD_EQ(a, b, instrument_id) &&
         a.side == b.side && FIELD_EQ(a, b, qty_lots) && FIELD_EQ(a, b, price_ticks) &&
         a.time_in_force == b.time_in_force && FIELD_EQ(a, b, param_version);
}

bool operator==(const v1::ApprovalIssuedPayload& a, const v1::ApprovalIssuedPayload& b) {
  return a.client_id == b.client_id && FIELD_EQ(a, b, limit_set_version) &&
         FIELD_EQ(a, b, expiry_mono_ns);
}

bool operator==(const v1::RiskRejectedPayload& a, const v1::RiskRejectedPayload& b) {
  return a.client_id == b.client_id && FIELD_EQ(a, b, limit_id) && FIELD_EQ(a, b, observed) &&
         FIELD_EQ(a, b, threshold);
}

bool operator==(const v1::OrderSentPayload& a, const v1::OrderSentPayload& b) {
  return a.client_id == b.client_id && a.endpoint_category == b.endpoint_category;
}

bool operator==(const v1::OrderEventPayload& a, const v1::OrderEventPayload& b) {
  return a.client_id == b.client_id && a.from_state == b.from_state && a.to_state == b.to_state &&
         a.broker_order_id == b.broker_order_id && FIELD_EQ(a, b, reason_code);
}

bool operator==(const v1::FillEventPayload& a, const v1::FillEventPayload& b) {
  return a.client_id == b.client_id && FIELD_EQ(a, b, fill_id) && FIELD_EQ(a, b, price_ticks) &&
         FIELD_EQ(a, b, qty_lots) && FIELD_EQ(a, b, fee_ticks) && FIELD_EQ(a, b, exchange_time_ns);
}

bool operator==(const v1::PositionSnapshotPayload& a, const v1::PositionSnapshotPayload& b) {
  return FIELD_EQ(a, b, instrument_id) && FIELD_EQ(a, b, net_qty_lots) &&
         FIELD_EQ(a, b, avg_price_ticks) && FIELD_EQ(a, b, realized_pnl_ticks);
}

bool operator==(const v1::ReconciliationResultPayload& a, const v1::ReconciliationResultPayload& b) {
  return a.scope == b.scope && a.outcome == b.outcome && FIELD_EQ(a, b, detail_code) &&
         a.detail_message == b.detail_message;
}

bool operator==(const v1::ControlCommandPayload& a, const v1::ControlCommandPayload& b) {
  return a.command == b.command && a.scope == b.scope && a.operator_ref == b.operator_ref &&
         a.outcome == b.outcome;
}

bool operator==(const v1::LimitTightenedPayload& a, const v1::LimitTightenedPayload& b) {
  return FIELD_EQ(a, b, limit_id) && FIELD_EQ(a, b, old_value) && FIELD_EQ(a, b, new_value) &&
         a.operator_ref == b.operator_ref;
}

bool operator==(const v1::StateTransitionPayload& a, const v1::StateTransitionPayload& b) {
  return a.domain == b.domain && FIELD_EQ(a, b, from_state) && FIELD_EQ(a, b, to_state) &&
         FIELD_EQ(a, b, reason_code);
}

bool operator==(const v1::ConfigRecordedPayload& a, const v1::ConfigRecordedPayload& b) {
  return a.config_hash == b.config_hash && a.content_ref_hash == b.content_ref_hash;
}

bool operator==(const v1::PromotionVerifiedPayload& a, const v1::PromotionVerifiedPayload& b) {
  return a.promotion_file_hash == b.promotion_file_hash && a.config_hash == b.config_hash &&
         a.outcome == b.outcome;
}

template <typename Payload, typename EncodeFn, typename DecodeFn>
bool test_record(const Payload& sample, EncodeFn encode, DecodeFn decode) {
  const auto bytes = encode(sample);
  Payload out{};
  if (!decode(bytes, out)) {
    return false;
  }
  return sample == out;
}

static v1::SessionStartPayload make_session_start() {
  v1::SessionStartPayload p{};
  p.profile_name = "paper-demo";
  p.config_hash.fill(std::byte{0x11});
  p.engine_build_hash.fill(std::byte{0x22});
  p.client_id_prefix = {'S', '7', 'F', '2'};
  return p;
}

int main(int argc, char** argv) {
  if (!test_record(make_session_start(), v1::encode_session_start, v1::decode_session_start)) {
    return 1;
  }

  v1::RecordHeader header{};
  header.magic[0] = 'T';
  header.magic[1] = 'J';
  header.magic[2] = 'N';
  header.magic[3] = 'L';
  header.format_version = v1::kFormatVersion;
  header.record_type = static_cast<std::uint16_t>(v1::RecordType::SessionStart);
  header.sequence = 42;
  header.monotonic_ns = 100;
  header.wall_ns = 200;
  const auto payload =
      v1::encode_session_start(make_session_start());
  const auto encoded =
      v1::encode_header(header, std::span<const std::byte>(payload.data(), payload.size()));
  v1::RecordHeader decoded{};
  if (!v1::decode_header(encoded, decoded)) {
    return 1;
  }
  if (decoded.sequence != header.sequence || decoded.payload_length != payload.size()) {
    return 1;
  }

  if (argc > 1 && std::string_view(argv[1]) == "--print-python-hex") {
    std::cout << std::hex << std::setfill('0');
    for (const auto b : payload) {
      std::cout << std::setw(2) << static_cast<unsigned>(static_cast<unsigned char>(b));
    }
    std::cout << std::dec << "\n";
    return 0;
  }

  // Remaining record types
  v1::SessionEndPayload session_end{
      v1::SessionEndReason::NormalShutdown, v1::EngineRunState::Running};
  if (!test_record(session_end, v1::encode_session_end, v1::decode_session_end)) {
    return 1;
  }

  v1::MarketQuotePayload quote{};
  quote.instrument_id = 7;
  quote.bid_ticks = 1000;
  quote.ask_ticks = 1005;
  quote.bid_size_lots = 2;
  quote.ask_size_lots = 3;
  quote.exchange_time_ns = 999;
  if (!test_record(quote, v1::encode_market_quote, v1::decode_market_quote)) {
    return 1;
  }

  v1::MarketTradePayload trade{};
  trade.instrument_id = 7;
  trade.price_ticks = 1002;
  trade.size_lots = 1;
  trade.side = v1::Side::Buy;
  trade.exchange_time_ns = 1000;
  if (!test_record(trade, v1::encode_market_trade, v1::decode_market_trade)) {
    return 1;
  }

  v1::FeedStatePayload feed{};
  feed.instrument_id = 7;
  feed.state = v1::FeedHealthState::Live;
  feed.reason_code = 0;
  if (!test_record(feed, v1::encode_feed_state, v1::decode_feed_state)) {
    return 1;
  }

  v1::OrderIntentPayload intent{};
  intent.client_id.fill('\0');
  const std::string cid = "S7F2-0000000001";
  std::copy_n(cid.begin(), std::min(cid.size(), intent.client_id.size()), intent.client_id.begin());
  intent.strategy_id = 3;
  intent.instrument_id = 7;
  intent.side = v1::Side::Buy;
  intent.qty_lots = 2;
  intent.price_ticks = 1000;
  intent.time_in_force = v1::TimeInForce::Day;
  intent.param_version = 1;
  if (!test_record(intent, v1::encode_order_intent, v1::decode_order_intent)) {
    return 1;
  }

  v1::ApprovalIssuedPayload approval{};
  approval.client_id = intent.client_id;
  approval.limit_set_version = 9;
  approval.expiry_mono_ns = 500000000;
  if (!test_record(approval, v1::encode_approval_issued, v1::decode_approval_issued)) {
    return 1;
  }

  v1::RiskRejectedPayload risk{};
  risk.client_id = intent.client_id;
  risk.limit_id = 2;
  risk.observed = 11;
  risk.threshold = 10;
  if (!test_record(risk, v1::encode_risk_rejected, v1::decode_risk_rejected)) {
    return 1;
  }

  v1::OrderSentPayload sent{};
  sent.client_id = intent.client_id;
  sent.endpoint_category = v1::EndpointCategory::OrderEntry;
  if (!test_record(sent, v1::encode_order_sent, v1::decode_order_sent)) {
    return 1;
  }

  v1::OrderEventPayload event{};
  event.client_id = intent.client_id;
  event.from_state = v1::OrderState::PendingNew;
  event.to_state = v1::OrderState::Acked;
  event.broker_order_id.fill('\0');
  const std::string boid = "BRK-123";
  std::copy_n(boid.begin(), std::min(boid.size(), event.broker_order_id.size()),
              event.broker_order_id.begin());
  event.reason_code = 0;
  if (!test_record(event, v1::encode_order_event, v1::decode_order_event)) {
    return 1;
  }

  v1::FillEventPayload fill{};
  fill.client_id = intent.client_id;
  fill.fill_id = 99;
  fill.price_ticks = 1001;
  fill.qty_lots = 1;
  fill.fee_ticks = 1;
  fill.exchange_time_ns = 1002;
  if (!test_record(fill, v1::encode_fill_event, v1::decode_fill_event)) {
    return 1;
  }

  v1::PositionSnapshotPayload pos{};
  pos.instrument_id = 7;
  pos.net_qty_lots = 1;
  pos.avg_price_ticks = 1001;
  pos.realized_pnl_ticks = -5;
  if (!test_record(pos, v1::encode_position_snapshot, v1::decode_position_snapshot)) {
    return 1;
  }

  v1::ReconciliationResultPayload recon{};
  recon.scope = v1::ReconciliationScope::Global;
  recon.outcome = v1::ReconciliationOutcome::Clean;
  recon.detail_code = 0;
  recon.detail_message = "ok";
  if (!test_record(recon, v1::encode_reconciliation_result, v1::decode_reconciliation_result)) {
    return 1;
  }

  v1::ControlCommandPayload control{};
  control.command = v1::ControlCommandKind::Halt;
  control.scope = v1::ControlScope::Global;
  control.operator_ref.fill('\0');
  const std::string op = "operator-ref-1";
  std::copy_n(op.begin(), std::min(op.size(), control.operator_ref.size()),
              control.operator_ref.begin());
  control.outcome = v1::CommandOutcome::Accepted;
  if (!test_record(control, v1::encode_control_command, v1::decode_control_command)) {
    return 1;
  }

  v1::LimitTightenedPayload tightened{};
  tightened.limit_id = 3;
  tightened.old_value = 100;
  tightened.new_value = 90;
  tightened.operator_ref = control.operator_ref;
  if (!test_record(tightened, v1::encode_limit_tightened, v1::decode_limit_tightened)) {
    return 1;
  }

  v1::StateTransitionPayload transition{};
  transition.domain = v1::StateDomain::Engine;
  transition.from_state = 1;
  transition.to_state = 2;
  transition.reason_code = 0;
  if (!test_record(transition, v1::encode_state_transition, v1::decode_state_transition)) {
    return 1;
  }

  v1::ConfigRecordedPayload config{};
  config.config_hash.fill(std::byte{0x33});
  config.content_ref_hash.fill(std::byte{0x44});
  if (!test_record(config, v1::encode_config_recorded, v1::decode_config_recorded)) {
    return 1;
  }

  v1::PromotionVerifiedPayload promo{};
  promo.promotion_file_hash.fill(std::byte{0x55});
  promo.config_hash.fill(std::byte{0x66});
  promo.outcome = v1::VerificationOutcome::Passed;
  if (!test_record(promo, v1::encode_promotion_verified, v1::decode_promotion_verified)) {
    return 1;
  }

  return 0;
}
