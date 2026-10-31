#pragma once

#include <cstdint>

namespace trading::strategy {

// Numeric codes match journal v1 Side and TimeInForce so a later mapper
// does not invent a translation. This header does not include the journal.
enum class Side : std::uint16_t { Buy = 1, Sell = 2 };

enum class TimeInForce : std::uint16_t {
  Day = 1,
  ImmediateOrCancel = 2,
  FillOrKill = 3,
};

// A strategy intent. The engine assigns the client order id later.
// There is no client order id field here.
struct OrderIntent {
  std::uint32_t strategy_id{0};
  std::uint32_t instrument_id{0};
  Side side{Side::Buy};
  std::int32_t qty_lots{0};
  std::int64_t price_ticks{0};
  TimeInForce time_in_force{TimeInForce::Day};
  std::uint64_t param_version{0};
  std::uint64_t snapshot_sequence{0};
};

} // namespace trading::strategy
