#include "broker_execution.hpp"

#include "trading/orders/execution_sink.hpp"
#include "trading/strategy/intent.hpp"

namespace trading::trader {

namespace {

[[nodiscard]] orders::ClientOrderId client_id_from_risk(const std::array<char, risk::kClientIdBytes>& bytes) {
  orders::ClientOrderId id{};
  for (std::size_t i = 0; i < id.bytes.size() && i < bytes.size(); ++i) {
    id.bytes[i] = bytes[i];
  }
  return id;
}

[[nodiscard]] orders::Side map_side(strategy::Side side) noexcept {
  return side == strategy::Side::Sell ? orders::Side::Sell : orders::Side::Buy;
}

} // namespace

bool accept_and_execute(const risk::ApprovedIngress& packet, risk::RiskGate& gate,
                         broker::IBroker& broker, const std::int64_t now_mono_ns,
                         orders::ExecutionSink& sink, BrokerExecutionStats& stats) noexcept {
  if (!orders::accept_for_execution(packet, gate, now_mono_ns, sink)) {
    return false;
  }
  if (packet.warmup) {
    return true;
  }

  broker::PlaceOrderRequest request{};
  request.client_id = client_id_from_risk(packet.client_id);
  request.product_id = packet.intent.instrument_id;
  request.size_lots = packet.intent.qty_lots;
  request.side = map_side(packet.intent.side);
  request.limit_price_ticks = packet.intent.price_ticks;

  const broker::SubmitResult submit = broker.place_order(request);
  if (submit.disposition == broker::SubmitDisposition::Acknowledged) {
    ++stats.broker_places;
  }

  broker.tick(now_mono_ns);
  const auto events = broker.poll_private_events();
  for (const auto& event : events) {
    if (event.kind == broker::PrivateEventKind::Fill) {
      ++stats.broker_fills;
      stats.last_fill_qty_lots += event.fill_qty_lots;
    }
  }
  return true;
}

} // namespace trading::trader
