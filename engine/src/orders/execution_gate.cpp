#include "trading/orders/execution_sink.hpp"

namespace trading::orders {

bool accept_for_execution(const risk::ApprovedIngress& packet, risk::RiskGate& gate,
                          std::int64_t now_ns, ExecutionSink& sink) noexcept {
  if (packet.warmup) {
    sink.completed.fetch_add(1, std::memory_order_release);
    return true;
  }
  const auto check = gate.accept_token(packet.token, packet.client_id, now_ns);
  if (check != risk::TokenCheck::Ok) {
    sink.last_rejected = packet;
    // Release: publish the rejection count after last_rejected is written.
    sink.rejected_token.fetch_add(1, std::memory_order_release);
    return false;
  }
  sink.last = packet;
  // Release: publish completed count after last is written.
  sink.completed.fetch_add(1, std::memory_order_release);
  return true;
}

} // namespace trading::orders
