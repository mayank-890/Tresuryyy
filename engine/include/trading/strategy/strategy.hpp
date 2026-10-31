#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "trading/strategy/intent.hpp"
#include "trading/strategy/market_snapshot.hpp"
#include "trading/strategy/param_buffer.hpp"

namespace trading::strategy {

// Strategies emit OrderIntent values only. Implementations must not call a
// broker, the journal writer, or a socket.
class IStrategy {
public:
  virtual ~IStrategy() = default;

  IStrategy(const IStrategy&) = delete;
  IStrategy& operator=(const IStrategy&) = delete;
  IStrategy(IStrategy&&) = delete;
  IStrategy& operator=(IStrategy&&) = delete;

  [[nodiscard]] virtual std::string_view name() const = 0;
  [[nodiscard]] virtual std::uint32_t strategy_id() const = 0;

  // Writes intents into out. Returns how many were written.
  // Zero is a valid decision. Must not allocate.
  [[nodiscard]] virtual std::size_t decide(const MarketSnapshot& market, const ParamView& params,
                                           std::span<OrderIntent> out) const = 0;

protected:
  IStrategy() = default;
};

} // namespace trading::strategy
