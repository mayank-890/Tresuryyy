#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "trading/strategy/strategy.hpp"

namespace trading::strategy::flat_echo {

// SAMPLE — NOT FOR TRADING. Synthetic fields only. qty_lots <= 0 emits nothing.
struct FlatParams {
  std::uint64_t version{0};
  std::uint32_t instrument_id{0};
  std::int32_t qty_lots{0};
  Side side{Side::Buy};
  TimeInForce time_in_force{TimeInForce::Day};
};

static_assert(std::is_trivially_copyable_v<FlatParams>);

// Pure. Not a risk limit. Non-positive quantities emit no lots.
[[nodiscard]] constexpr std::int32_t size_lots(const FlatParams& params) {
  if (params.qty_lots <= 0) {
    return 0;
  }
  return params.qty_lots;
}

// TODO(operator): production strategy ids are not assigned here.
inline constexpr std::uint32_t kSampleStrategyId = 1;

// Sample strategy. At most one intent: buy at the ask, sell at the bid.
class FlatEcho final : public IStrategy {
public:
  static constexpr std::string_view kName{"flat_echo"};

  explicit FlatEcho(std::uint32_t strategy_id) : strategy_id_(strategy_id) {}

  [[nodiscard]] std::string_view name() const override { return kName; }
  [[nodiscard]] std::uint32_t strategy_id() const override { return strategy_id_; }

  [[nodiscard]] std::size_t decide(const MarketSnapshot& market, const ParamView& params,
                                   std::span<OrderIntent> out) const override;

private:
  std::uint32_t strategy_id_;
};

} // namespace trading::strategy::flat_echo
