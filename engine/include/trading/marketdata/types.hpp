#pragma once

#include "trading/journal/generated/records.hpp"

#include <cstdint>

namespace trading::marketdata {

// DESIGN §8.1 — prices in ticks; sizes in lots.
struct Quote {
  std::uint32_t instrument_id{0};
  std::int64_t bid_ticks{0};
  std::int64_t ask_ticks{0};
  std::int32_t bid_size_lots{0};
  std::int32_t ask_size_lots{0};
  std::uint64_t exchange_time_ns{0};
  std::uint64_t receive_mono_ns{0};
  std::uint64_t sequence{0};
};

struct Trade {
  std::uint32_t instrument_id{0};
  std::int64_t price_ticks{0};
  std::int32_t size_lots{0};
  journal::v1::Side side{journal::v1::Side::Unknown};
  std::uint64_t exchange_time_ns{0};
  std::uint64_t receive_mono_ns{0};
  std::uint64_t sequence{0};
};

enum class InstrumentHealth : std::uint8_t {
  Healthy = 1,
  Stale = 2,
  Resyncing = 3,
  Down = 4,
};

} // namespace trading::marketdata
