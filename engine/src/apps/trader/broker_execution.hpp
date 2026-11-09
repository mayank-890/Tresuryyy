#pragma once

#include "trading/broker/broker.hpp"
#include "trading/orders/execution_sink.hpp"
#include "trading/risk/gate.hpp"
#include "trading/risk/pipeline_types.hpp"

#include <cstdint>

namespace trading::trader {

struct BrokerExecutionStats {
  std::uint32_t broker_places{0};
  std::uint32_t broker_fills{0};
  std::int32_t last_fill_qty_lots{0};
};

[[nodiscard]] bool accept_and_execute(const risk::ApprovedIngress& packet, risk::RiskGate& gate,
                                       broker::IBroker& broker, std::int64_t now_mono_ns,
                                       orders::ExecutionSink& sink,
                                       BrokerExecutionStats& stats) noexcept;

} // namespace trading::trader
