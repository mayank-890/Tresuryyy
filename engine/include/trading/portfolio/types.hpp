#pragma once

#include "trading/orders/types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace trading::portfolio {

struct PositionRow {
  std::uint32_t instrument_id{0};
  std::int32_t net_qty_lots{0};
  std::int64_t avg_price_ticks{0};
  std::int64_t realized_pnl_ticks{0};
};

struct OpenOrderRow {
  orders::ClientOrderId client_id{};
  std::uint32_t instrument_id{0};
  orders::Side side{orders::Side::Buy};
  std::int32_t qty_lots{0};
  std::int64_t price_ticks{0};
};

struct FillRow {
  std::uint64_t fill_id{0};
  std::uint32_t instrument_id{0};
  std::int32_t qty_lots{0};
  std::int64_t price_ticks{0};
};

// Internal book view and broker REST snapshot share the same layout in this ticket.
struct PortfolioSnapshot {
  std::vector<PositionRow> positions{};
  std::vector<OpenOrderRow> open_orders{};
  std::vector<FillRow> fills{};
};

[[nodiscard]] bool client_id_less(const orders::ClientOrderId& lhs,
                                  const orders::ClientOrderId& rhs) noexcept;

} // namespace trading::portfolio
