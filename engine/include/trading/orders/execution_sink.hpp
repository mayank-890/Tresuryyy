#pragma once

#include "trading/risk/pipeline_types.hpp"

#include <atomic>
#include <cstdint>

namespace trading::orders {

// Written only by T4 Execution. Tests read after pipeline stop.
struct ExecutionSink {
  std::atomic<std::uint32_t> completed{0};
  std::atomic<std::uint32_t> rejected_token{0};
  risk::ApprovedIngress last{};
  risk::ApprovedIngress last_rejected{};
};

[[nodiscard]] bool accept_for_execution(const risk::ApprovedIngress& packet, risk::RiskGate& gate,
                                        std::int64_t now_ns, ExecutionSink& sink) noexcept;

} // namespace trading::orders
