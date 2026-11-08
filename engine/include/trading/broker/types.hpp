#pragma once

#include "trading/journal/generated/records.hpp"
#include "trading/orders/types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace trading::broker {

enum class BrokerHealthState : std::uint8_t {
  Disconnected = 1,
  Connecting = 2,
  Connected = 3,
  Degraded = 4,
  Halted = 5,
};

struct BrokerHealth {
  BrokerHealthState state{BrokerHealthState::Disconnected};
  bool rest_ok{false};
  bool private_ws_ok{false};
};

enum class SubmitDisposition : std::uint8_t {
  Acknowledged = 1,
  Rejected = 2,
  Ambiguous = 3,
};

struct SubmitResult {
  SubmitDisposition disposition{SubmitDisposition::Ambiguous};
  std::uint64_t broker_order_id{0};
  std::string reason;
};

enum class CancelDisposition : std::uint8_t {
  Confirmed = 1,
  Rejected = 2,
  Ambiguous = 3,
};

struct CancelResult {
  CancelDisposition disposition{CancelDisposition::Ambiguous};
  std::string reason;
};

enum class QueryDisposition : std::uint8_t {
  Found = 1,
  NotFound = 2,
  Error = 3,
};

struct OrderSnapshot {
  orders::ClientOrderId client_id{};
  orders::OrderState state{orders::OrderState::Unknown};
  std::int32_t qty_lots{0};
  std::int32_t filled_qty_lots{0};
  std::uint64_t broker_order_id{0};
};

struct QueryResult {
  QueryDisposition disposition{QueryDisposition::Error};
  OrderSnapshot order{};
  std::string reason;
};

struct PlaceOrderRequest {
  orders::ClientOrderId client_id{};
  std::uint32_t product_id{0};
  orders::Side side{orders::Side::Buy};
  std::int32_t size_lots{0};
  std::int64_t limit_price_ticks{0};
  bool reduce_only{false};
};

enum class PrivateEventKind : std::uint8_t {
  OrderUpdate = 1,
  Fill = 2,
  PositionUpdate = 3,
  Heartbeat = 4,
  Connected = 5,
  Disconnected = 6,
};

struct PrivateEvent {
  PrivateEventKind kind{PrivateEventKind::Heartbeat};
  orders::ClientOrderId client_id{};
  orders::OrderState order_state{orders::OrderState::Unknown};
  std::int32_t fill_qty_lots{0};
  std::int64_t fill_price_ticks{0};
  std::int32_t position_qty_lots{0};
  std::uint32_t product_id{0};
};

struct ApiCredentials {
  std::string api_key;
  std::string api_secret;
};

struct CredentialPort {
  void* ctx{nullptr};
  bool (*load)(void* ctx, ApiCredentials& out){nullptr};
};

} // namespace trading::broker
