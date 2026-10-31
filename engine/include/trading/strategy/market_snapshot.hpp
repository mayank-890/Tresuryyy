#pragma once

#include <cstdint>
#include <span>

namespace trading::strategy {

struct Quote {
  std::uint32_t instrument_id{0};
  std::int64_t bid_ticks{0};
  std::int64_t ask_ticks{0};
  std::int32_t bid_size_lots{0};
  std::int32_t ask_size_lots{0};
  std::uint64_t exchange_time_ns{0};
};

// Immutable view. The caller owns the quote storage for the cycle.
struct MarketSnapshot {
  std::uint64_t sequence{0};
  std::span<const Quote> quotes{};
};

} // namespace trading::strategy
