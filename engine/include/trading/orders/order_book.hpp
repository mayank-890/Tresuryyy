#pragma once

#include "trading/core/result.hpp"
#include "trading/orders/client_order_id.hpp"
#include "trading/orders/types.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace trading::orders {

inline constexpr int kUnknownQueryAttempts = 3;
inline constexpr std::uint32_t kUnknownQueryDelayBeforeSecondMs = 1000;
inline constexpr std::uint32_t kUnknownQueryDelayBeforeThirdMs = 3000;
inline constexpr std::uint16_t kAlertAl04 = 4;

struct JournalPort {
  void* ctx{nullptr};
  bool (*record_intent)(void* ctx, const IntentRequest& request, const ClientOrderId& id){nullptr};
  bool (*record_transition)(void* ctx, const ClientOrderId& id, OrderState from, OrderState to,
                            std::uint16_t reason){nullptr};
};

struct SendPort {
  void* ctx{nullptr};
  void (*send_new)(void* ctx, const IntentRequest& request, const ClientOrderId& id){nullptr};
  void (*send_cancel)(void* ctx, const ClientOrderId& id){nullptr};
};

struct QueryResponse {
  enum class Status : std::uint8_t { Found, NotFound, Failed };
  Status status{Status::Failed};
  OrderState state{OrderState::Unknown};
  std::int32_t reported_filled_qty{0};
};

struct ResendResponse {
  enum class Status : std::uint8_t { Resolved, NotFound, Unresolved };
  Status status{Status::Unresolved};
  OrderState state{OrderState::Unknown};
  std::int32_t reported_filled_qty{0};
};

// Query and resend both receive the existing client id. There is no field
// for a replacement id (DESIGN §5.4).
struct LookupPort {
  void* ctx{nullptr};
  QueryResponse (*query_by_client_id)(void* ctx, std::string_view client_id){nullptr};
  ResendResponse (*resend_same_client_id)(void* ctx, const IntentRequest& request,
                                          const ClientOrderId& id){nullptr};
};

struct DelayPort {
  void* ctx{nullptr};
  void (*delay_ms)(void* ctx, std::uint32_t milliseconds){nullptr};
};

struct TransitionLog {
  ClientOrderId client_id{};
  OrderState from{OrderState::IntentJournaled};
  OrderEventKind event{OrderEventKind::SentToBroker};
  bool rejected{false};
  char message[32]{};
};

struct DefectRecord {
  ClientOrderId client_id{};
  OrderState from{OrderState::IntentJournaled};
  OrderEventKind event{OrderEventKind::SentToBroker};
};

struct AlertRecord {
  std::uint16_t code{kAlertAl04};
  ClientOrderId client_id{};
};

struct StoredOrder {
  ClientOrderId client_id{};
  IntentRequest request{};
  OrderState state{OrderState::IntentJournaled};
  std::int32_t filled_qty_lots{0};
  RejectReason reject_reason{RejectReason::None};
  bool intent_durable{false};
  bool unknown_resend_attempted{false};
  std::uint32_t duplicate_fill_count{0};
  std::vector<std::uint64_t> fill_ids{};
};

class OrderBook {
public:
  OrderBook(IdSource& ids, JournalPort journal, SendPort send);

  OrderBook(const OrderBook&) = delete;
  OrderBook& operator=(const OrderBook&) = delete;

  // Stores the order only after `record_intent` returns true.
  [[nodiscard]] core::Result<ClientOrderId, OrderError> open_intent(const IntentRequest& request);

  // Journals the sent transition, then calls `send_new`. No call if the
  // intent was not durable or the transition journal fails.
  [[nodiscard]] core::Result<OrderState, OrderError> send(const ClientOrderId& id);

  [[nodiscard]] core::Result<OrderState, OrderError> apply(const ClientOrderId& id,
                                                           const OrderEvent& event);

  // DESIGN §5.4. Queries by the existing client id, then resends that same
  // id at most once. Does not call the id source.
  [[nodiscard]] core::Result<OrderState, OrderError> resolve_unknown(const ClientOrderId& id,
                                                                     LookupPort lookup,
                                                                     DelayPort delay);

  [[nodiscard]] const StoredOrder* find(const ClientOrderId& id) const;
  [[nodiscard]] std::size_t order_count() const { return orders_.size(); }
  [[nodiscard]] bool scope_halted(std::uint32_t strategy_id, std::uint32_t instrument_id) const;
  [[nodiscard]] bool scope_blocked(std::uint32_t strategy_id, std::uint32_t instrument_id) const;
  [[nodiscard]] const std::vector<DefectRecord>& defects() const { return defects_; }
  [[nodiscard]] const std::vector<TransitionLog>& logs() const { return logs_; }
  [[nodiscard]] const std::vector<AlertRecord>& alerts() const { return alerts_; }

private:
  struct ScopeKey {
    std::uint32_t strategy_id{0};
    std::uint32_t instrument_id{0};
  };

  [[nodiscard]] const StoredOrder* find_const(const ClientOrderId& id) const;
  [[nodiscard]] StoredOrder* find_mut(const ClientOrderId& id);
  [[nodiscard]] core::Result<OrderState, OrderError> commit(StoredOrder& order, const OrderEvent& event);
  void log_reject(const StoredOrder& order, const OrderEvent& event);
  void raise_defect(const StoredOrder& order, const OrderEvent& event);
  void halt(std::uint32_t strategy_id, std::uint32_t instrument_id);
  void block(std::uint32_t strategy_id, std::uint32_t instrument_id);
  void clear_block_if_idle(std::uint32_t strategy_id, std::uint32_t instrument_id);
  void raise_al04(const ClientOrderId& id);
  [[nodiscard]] bool has_scope(const std::vector<ScopeKey>& keys, std::uint32_t strategy_id,
                               std::uint32_t instrument_id) const;

  IdSource& ids_;
  JournalPort journal_{};
  SendPort send_{};
  std::vector<StoredOrder> orders_{};
  std::vector<ScopeKey> halted_{};
  std::vector<ScopeKey> blocked_{};
  std::vector<DefectRecord> defects_{};
  std::vector<TransitionLog> logs_{};
  std::vector<AlertRecord> alerts_{};
};

} // namespace trading::orders
