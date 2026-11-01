#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace trading::orders {

// Journal v1 stores client_id as 24 ASCII bytes. The visible form is 15
// characters: 4-character session prefix, hyphen, 10-digit counter.
inline constexpr std::size_t kClientIdStorage = 24;
inline constexpr std::size_t kSessionPrefixLen = 4;
inline constexpr std::size_t kClientIdCounterDigits = 10;
inline constexpr std::size_t kClientIdTextLen = kSessionPrefixLen + 1 + kClientIdCounterDigits;
inline constexpr std::uint64_t kMaxClientCounter = 9'999'999'999ULL;

// Numeric values match journal v1 OrderState. This header does not include
// the generated journal records.
enum class OrderState : std::uint16_t {
  IntentJournaled = 1,
  PendingNew = 2,
  Acked = 3,
  PartiallyFilled = 4,
  Filled = 5,
  PendingCancel = 6,
  Cancelled = 7,
  Rejected = 8,
  Expired = 9,
  Unknown = 10,
};

enum class Side : std::uint16_t { Buy = 1, Sell = 2 };

enum class TimeInForce : std::uint16_t {
  Day = 1,
  ImmediateOrCancel = 2,
  FillOrKill = 3,
};

enum class RejectReason : std::uint16_t {
  None = 0,
  LocalValidation = 1,
  RiskGate = 2,
  Broker = 3,
  NotFoundAfterQuery = 4,
};

enum class OrderEventKind : std::uint8_t {
  SentToBroker = 1,
  LocalReject = 2,
  BrokerAck = 3,
  BrokerDefinitiveReject = 4,
  TimeoutOrAmbiguous = 5,
  Fill = 6,
  CancelRequested = 7,
  CancelConfirmed = 8,
  ExchangeCancel = 9,
  Expiry = 10,
  QueryFound = 11,
  QueryNotFoundAfterRetry = 12,
};

// TECHNICAL_IMPLEMENTATION.md §6. Extra values cover the journal gate,
// scope halt, and the client-id generator.
enum class OrderError : std::uint8_t {
  InvalidTransition = 1,
  UnknownClientId = 2,
  DuplicateIntent = 3,
  IntentNotDurable = 4,
  ScopeHalted = 5,
  ScopeBlocked = 6,
  JournalFailed = 7,
  InvalidQuantity = 8,
  BadPrefix = 9,
  CounterExhausted = 10,
  NotUnknown = 11,
  AwaitingOperator = 12,
};

struct ClientOrderId {
  std::array<char, kClientIdStorage> bytes{};

  [[nodiscard]] std::string_view text() const {
    return std::string_view(bytes.data(), kClientIdTextLen);
  }

  [[nodiscard]] bool empty() const { return bytes[0] == '\0'; }

  friend bool operator==(const ClientOrderId& lhs, const ClientOrderId& rhs) {
    return lhs.text() == rhs.text();
  }
};

struct OrderEvent {
  OrderEventKind kind{OrderEventKind::SentToBroker};
  std::int32_t fill_qty_lots{0};
  std::uint64_t fill_id{0};
  OrderState queried_state{OrderState::Unknown};
  std::int32_t reported_filled_qty{0};
  RejectReason reject_reason{RejectReason::None};
};

struct OrderView {
  OrderState state{OrderState::IntentJournaled};
  std::int32_t qty_lots{0};
  std::int32_t filled_qty_lots{0};
  RejectReason reject_reason{RejectReason::None};
};

struct IntentRequest {
  std::uint32_t strategy_id{0};
  std::uint32_t instrument_id{0};
  Side side{Side::Buy};
  std::int32_t qty_lots{0};
  std::int64_t price_ticks{0};
  TimeInForce time_in_force{TimeInForce::Day};
  std::uint32_t param_version{0};
};

[[nodiscard]] constexpr bool is_terminal(OrderState state) {
  switch (state) {
  case OrderState::Filled:
  case OrderState::Cancelled:
  case OrderState::Rejected:
  case OrderState::Expired:
    return true;
  case OrderState::IntentJournaled:
  case OrderState::PendingNew:
  case OrderState::Acked:
  case OrderState::PartiallyFilled:
  case OrderState::PendingCancel:
  case OrderState::Unknown:
    return false;
  }
  return false;
}

} // namespace trading::orders
