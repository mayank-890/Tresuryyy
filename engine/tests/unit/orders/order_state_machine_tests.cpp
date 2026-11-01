#include "trading/orders/client_order_id.hpp"
#include "trading/orders/order_book.hpp"
#include "trading/orders/transition.hpp"

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using trading::orders::ClientOrderId;
using trading::orders::ClientOrderIdGenerator;
using trading::orders::DelayPort;
using trading::orders::IdSource;
using trading::orders::IntentRequest;
using trading::orders::JournalPort;
using trading::orders::LookupPort;
using trading::orders::OrderBook;
using trading::orders::OrderError;
using trading::orders::OrderEvent;
using trading::orders::OrderEventKind;
using trading::orders::OrderState;
using trading::orders::OrderView;
using trading::orders::QueryResponse;
using trading::orders::RejectReason;
using trading::orders::ResendResponse;
using trading::orders::SendPort;
using trading::orders::Side;
using trading::orders::TimeInForce;
using trading::orders::apply_transition;
using trading::orders::format_client_order_id;
using trading::orders::is_terminal;
using trading::orders::kAlertAl04;
using trading::orders::kMaxClientCounter;
using trading::orders::session_prefix_from_counter;

static_assert(is_terminal(OrderState::Filled));
static_assert(is_terminal(OrderState::Cancelled));
static_assert(is_terminal(OrderState::Rejected));
static_assert(is_terminal(OrderState::Expired));
static_assert(!is_terminal(OrderState::IntentJournaled));
static_assert(!is_terminal(OrderState::PendingNew));
static_assert(!is_terminal(OrderState::Acked));
static_assert(!is_terminal(OrderState::PartiallyFilled));
static_assert(!is_terminal(OrderState::PendingCancel));
static_assert(!is_terminal(OrderState::Unknown));
static_assert(static_cast<std::uint16_t>(OrderState::IntentJournaled) == 1);
static_assert(static_cast<std::uint16_t>(OrderState::Unknown) == 10);

struct JournalProbe {
  bool accept_intent{true};
  bool accept_counter{true};
  bool accept_transition{true};
  int intents{0};
  IntentRequest last_intent{};
  ClientOrderId last_intent_id{};
  std::vector<std::uint64_t> counters;
  struct Jump {
    ClientOrderId id{};
    OrderState from{OrderState::IntentJournaled};
    OrderState to{OrderState::IntentJournaled};
    std::uint16_t reason{0};
  };
  std::vector<Jump> transitions;
};

struct SendProbe {
  int news{0};
  int cancels{0};
  ClientOrderId last_new{};
  ClientOrderId last_cancel{};
  IntentRequest last_request{};
};

struct LookupProbe {
  std::vector<QueryResponse> script;
  std::size_t script_at{0};
  ResendResponse resend{};
  int queries{0};
  int resends{0};
  std::string query_id;
  std::string resend_id;
  IntentRequest resend_request{};
  std::vector<std::uint32_t> delays;
};

bool persist_counter(void* ctx, std::uint64_t counter) {
  auto* probe = static_cast<JournalProbe*>(ctx);
  if (!probe->accept_counter) {
    return false;
  }
  probe->counters.push_back(counter);
  return true;
}

bool record_intent(void* ctx, const IntentRequest& request, const ClientOrderId& id) {
  auto* probe = static_cast<JournalProbe*>(ctx);
  if (!probe->accept_intent) {
    return false;
  }
  ++probe->intents;
  probe->last_intent = request;
  probe->last_intent_id = id;
  return true;
}

bool record_transition(void* ctx, const ClientOrderId& id, OrderState from, OrderState to,
                       std::uint16_t reason) {
  auto* probe = static_cast<JournalProbe*>(ctx);
  if (!probe->accept_transition) {
    return false;
  }
  probe->transitions.push_back(JournalProbe::Jump{id, from, to, reason});
  return true;
}

void send_new(void* ctx, const IntentRequest& request, const ClientOrderId& id) {
  auto* probe = static_cast<SendProbe*>(ctx);
  ++probe->news;
  probe->last_request = request;
  probe->last_new = id;
}

void send_cancel(void* ctx, const ClientOrderId& id) {
  auto* probe = static_cast<SendProbe*>(ctx);
  ++probe->cancels;
  probe->last_cancel = id;
}

QueryResponse query_by_client_id(void* ctx, std::string_view client_id) {
  auto* probe = static_cast<LookupProbe*>(ctx);
  ++probe->queries;
  probe->query_id = std::string(client_id);
  if (probe->script_at >= probe->script.size()) {
    return QueryResponse{};
  }
  return probe->script[probe->script_at++];
}

ResendResponse resend_same_client_id(void* ctx, const IntentRequest& request, const ClientOrderId& id) {
  auto* probe = static_cast<LookupProbe*>(ctx);
  ++probe->resends;
  probe->resend_id = std::string(id.text());
  probe->resend_request = request;
  return probe->resend;
}

void delay_ms(void* ctx, std::uint32_t milliseconds) {
  static_cast<LookupProbe*>(ctx)->delays.push_back(milliseconds);
}

JournalPort journal_port(JournalProbe* probe) {
  JournalPort port;
  port.ctx = probe;
  port.record_intent = record_intent;
  port.record_transition = record_transition;
  return port;
}

SendPort send_port(SendProbe* probe) {
  SendPort port;
  port.ctx = probe;
  port.send_new = send_new;
  port.send_cancel = send_cancel;
  return port;
}

LookupPort lookup_port(LookupProbe* probe) {
  LookupPort port;
  port.ctx = probe;
  port.query_by_client_id = query_by_client_id;
  port.resend_same_client_id = resend_same_client_id;
  return port;
}

DelayPort delay_port(LookupProbe* probe) {
  DelayPort port;
  port.ctx = probe;
  port.delay_ms = delay_ms;
  return port;
}

IntentRequest sample(std::uint32_t strategy = 7, std::uint32_t instrument = 11,
                     std::int32_t qty = 10) {
  IntentRequest request;
  request.strategy_id = strategy;
  request.instrument_id = instrument;
  request.side = Side::Buy;
  request.qty_lots = qty;
  request.price_ticks = 100;
  request.time_in_force = TimeInForce::Day;
  request.param_version = 3;
  return request;
}

OrderEvent event_kind(OrderEventKind kind) {
  OrderEvent event;
  event.kind = kind;
  return event;
}

struct Harness {
  JournalProbe journal;
  SendProbe sends;
  ClientOrderIdGenerator ids;
  OrderBook book;

  Harness()
      : ids("S7F2", 1, persist_counter, &journal), book(ids, journal_port(&journal), send_port(&sends)) {}
};

class ScriptedIds final : public IdSource {
public:
  explicit ScriptedIds(std::vector<ClientOrderId> ids) : ids_(std::move(ids)) {}

  trading::core::Result<ClientOrderId, OrderError> next() override {
    if (index_ >= ids_.size()) {
      return trading::core::Result<ClientOrderId, OrderError>::err(OrderError::CounterExhausted);
    }
    return trading::core::Result<ClientOrderId, OrderError>::ok(ids_[index_++]);
  }

private:
  std::vector<ClientOrderId> ids_;
  std::size_t index_{0};
};

struct Allowed {
  OrderState state;
  OrderEventKind kind;
};

constexpr Allowed kAllowed[] = {
    {OrderState::IntentJournaled, OrderEventKind::SentToBroker},
    {OrderState::IntentJournaled, OrderEventKind::LocalReject},
    {OrderState::PendingNew, OrderEventKind::BrokerAck},
    {OrderState::PendingNew, OrderEventKind::BrokerDefinitiveReject},
    {OrderState::PendingNew, OrderEventKind::TimeoutOrAmbiguous},
    {OrderState::Acked, OrderEventKind::Fill},
    {OrderState::PartiallyFilled, OrderEventKind::Fill},
    {OrderState::PendingCancel, OrderEventKind::Fill},
    {OrderState::Acked, OrderEventKind::CancelRequested},
    {OrderState::PartiallyFilled, OrderEventKind::CancelRequested},
    {OrderState::PendingCancel, OrderEventKind::CancelConfirmed},
    {OrderState::Acked, OrderEventKind::ExchangeCancel},
    {OrderState::PartiallyFilled, OrderEventKind::ExchangeCancel},
    {OrderState::Acked, OrderEventKind::Expiry},
    {OrderState::PartiallyFilled, OrderEventKind::Expiry},
    {OrderState::Unknown, OrderEventKind::QueryFound},
    {OrderState::Unknown, OrderEventKind::QueryNotFoundAfterRetry},
};

constexpr OrderState kStates[] = {
    OrderState::IntentJournaled, OrderState::PendingNew,      OrderState::Acked,
    OrderState::PartiallyFilled, OrderState::Filled,          OrderState::PendingCancel,
    OrderState::Cancelled,       OrderState::Rejected,        OrderState::Expired,
    OrderState::Unknown,
};

constexpr OrderEventKind kKinds[] = {
    OrderEventKind::SentToBroker,     OrderEventKind::LocalReject,
    OrderEventKind::BrokerAck,        OrderEventKind::BrokerDefinitiveReject,
    OrderEventKind::TimeoutOrAmbiguous, OrderEventKind::Fill,
    OrderEventKind::CancelRequested,  OrderEventKind::CancelConfirmed,
    OrderEventKind::ExchangeCancel,   OrderEventKind::Expiry,
    OrderEventKind::QueryFound,       OrderEventKind::QueryNotFoundAfterRetry,
};

bool table_allows(OrderState state, OrderEventKind kind) {
  for (const Allowed& row : kAllowed) {
    if (row.state == state && row.kind == kind) {
      return true;
    }
  }
  return false;
}

OrderEvent canonical(OrderEventKind kind) {
  OrderEvent event;
  event.kind = kind;
  event.fill_qty_lots = 1;
  event.fill_id = 99;
  event.queried_state = OrderState::Acked;
  event.reject_reason = RejectReason::LocalValidation;
  return event;
}

bool drive_to(Harness& harness, const ClientOrderId& id, OrderState target) {
  OrderBook& book = harness.book;
  if (target == OrderState::IntentJournaled) {
    return book.find(id) != nullptr && book.find(id)->state == target;
  }
  if (target == OrderState::Rejected) {
    OrderEvent event = event_kind(OrderEventKind::LocalReject);
    event.reject_reason = RejectReason::LocalValidation;
    return static_cast<bool>(book.apply(id, event)) && book.find(id)->state == target;
  }
  if (!book.send(id)) {
    return false;
  }
  if (target == OrderState::PendingNew) {
    return book.find(id)->state == target;
  }
  if (target == OrderState::Unknown) {
    return static_cast<bool>(book.apply(id, event_kind(OrderEventKind::TimeoutOrAmbiguous))) &&
           book.find(id)->state == target;
  }
  if (!book.apply(id, event_kind(OrderEventKind::BrokerAck))) {
    return false;
  }
  if (target == OrderState::Acked) {
    return book.find(id)->state == target;
  }
  if (target == OrderState::PartiallyFilled) {
    OrderEvent fill = event_kind(OrderEventKind::Fill);
    fill.fill_qty_lots = 4;
    fill.fill_id = 1;
    return static_cast<bool>(book.apply(id, fill)) && book.find(id)->state == target;
  }
  if (target == OrderState::Filled) {
    OrderEvent fill = event_kind(OrderEventKind::Fill);
    fill.fill_qty_lots = 10;
    fill.fill_id = 1;
    return static_cast<bool>(book.apply(id, fill)) && book.find(id)->state == target;
  }
  if (target == OrderState::Expired) {
    return static_cast<bool>(book.apply(id, event_kind(OrderEventKind::Expiry))) &&
           book.find(id)->state == target;
  }
  if (target == OrderState::PendingCancel || target == OrderState::Cancelled) {
    if (!book.apply(id, event_kind(OrderEventKind::CancelRequested))) {
      return false;
    }
    if (target == OrderState::PendingCancel) {
      return book.find(id)->state == target;
    }
    return static_cast<bool>(book.apply(id, event_kind(OrderEventKind::CancelConfirmed))) &&
           book.find(id)->state == target;
  }
  return false;
}

bool expect_move(const OrderView& view, const OrderEvent& event, OrderState to, std::int32_t filled,
                 RejectReason reason) {
  const auto result = apply_transition(view, event, false);
  EXPECT_TRUE(result);
  if (!result) {
    return false;
  }
  EXPECT_EQ(result.value().state, to);
  EXPECT_EQ(result.value().filled_qty_lots, filled);
  EXPECT_EQ(result.value().reject_reason, reason);
  EXPECT_FALSE(result.value().duplicate_fill);
  return result.value().state == to && result.value().filled_qty_lots == filled &&
         result.value().reject_reason == reason;
}

ClientOrderId open_sample(Harness& harness, const IntentRequest& request = sample()) {
  auto opened = harness.book.open_intent(request);
  EXPECT_TRUE(opened);
  if (!opened) {
    return ClientOrderId{};
  }
  return opened.value();
}

} // namespace

TEST(ClientOrderId, SessionPrefixIsBase36) {
  const auto zero = session_prefix_from_counter(0);
  const auto one = session_prefix_from_counter(1);
  const auto ten = session_prefix_from_counter(10);
  const auto thirty_five = session_prefix_from_counter(35);
  const auto thirty_six = session_prefix_from_counter(36);
  ASSERT_TRUE(zero);
  ASSERT_TRUE(one);
  ASSERT_TRUE(ten);
  ASSERT_TRUE(thirty_five);
  ASSERT_TRUE(thirty_six);
  EXPECT_EQ(std::string(zero.value().data(), zero.value().size()), "0000");
  EXPECT_EQ(std::string(one.value().data(), 4), "0001");
  EXPECT_EQ(std::string(ten.value().data(), 4), "000A");
  EXPECT_EQ(std::string(thirty_five.value().data(), 4), "000Z");
  EXPECT_EQ(std::string(thirty_six.value().data(), 4), "0010");

  constexpr std::uint32_t kSpace = 36U * 36U * 36U * 36U;
  const auto last = session_prefix_from_counter(kSpace - 1);
  const auto overflow = session_prefix_from_counter(kSpace);
  ASSERT_TRUE(last);
  EXPECT_EQ(std::string(last.value().data(), 4), "ZZZZ");
  EXPECT_FALSE(overflow);
  EXPECT_EQ(overflow.error(), OrderError::CounterExhausted);
}

TEST(ClientOrderId, FormatAndGenerator) {
  const auto example = format_client_order_id("S7F2", 12345);
  ASSERT_TRUE(example);
  EXPECT_EQ(example.value().text(), std::string_view{"S7F2-0000012345"});

  const auto lower = format_client_order_id("s7f2", 1);
  EXPECT_FALSE(lower);
  EXPECT_EQ(lower.error(), OrderError::BadPrefix);
  const auto short_prefix = format_client_order_id("S7F", 1);
  EXPECT_FALSE(short_prefix);
  EXPECT_EQ(short_prefix.error(), OrderError::BadPrefix);
  const auto too_big = format_client_order_id("S7F2", kMaxClientCounter + 1);
  EXPECT_FALSE(too_big);
  EXPECT_EQ(too_big.error(), OrderError::CounterExhausted);

  JournalProbe probe;
  ClientOrderIdGenerator generator("S7F2", 12345, persist_counter, &probe);
  auto first = generator.next();
  auto second = generator.next();
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(first.value().text(), std::string_view{"S7F2-0000012345"});
  EXPECT_EQ(second.value().text(), std::string_view{"S7F2-0000012346"});
  EXPECT_NE(first.value(), second.value());
  ASSERT_EQ(probe.counters.size(), 2U);
  EXPECT_EQ(probe.counters[0], 12345U);
  EXPECT_EQ(probe.counters[1], 12346U);

  probe.accept_counter = false;
  const auto held = generator.next_counter();
  auto rejected = generator.next();
  EXPECT_FALSE(rejected);
  EXPECT_EQ(rejected.error(), OrderError::JournalFailed);
  EXPECT_EQ(generator.next_counter(), held);
  probe.accept_counter = true;
  auto resumed = generator.next();
  ASSERT_TRUE(resumed);
  EXPECT_EQ(resumed.value().text(), std::string_view{"S7F2-0000012347"});

  int calls = 0;
  ClientOrderIdGenerator bad("s7f2", 1,
                             [](void* ctx, std::uint64_t) {
                               *static_cast<int*>(ctx) += 1;
                               return true;
                             },
                             &calls);
  auto bad_id = bad.next();
  EXPECT_FALSE(bad_id);
  EXPECT_EQ(bad_id.error(), OrderError::BadPrefix);
  EXPECT_EQ(calls, 0);

  JournalProbe exhausted_probe;
  ClientOrderIdGenerator exhausted("ABCD", kMaxClientCounter, persist_counter, &exhausted_probe);
  auto last = exhausted.next();
  ASSERT_TRUE(last);
  EXPECT_EQ(last.value().text(), std::string_view{"ABCD-9999999999"});
  auto past = exhausted.next();
  EXPECT_FALSE(past);
  EXPECT_EQ(past.error(), OrderError::CounterExhausted);
  EXPECT_EQ(exhausted_probe.counters.size(), 1U);

  ClientOrderIdGenerator other("ZZZZ", 1, persist_counter, &probe);
  auto other_id = other.next();
  ASSERT_TRUE(other_id);
  EXPECT_EQ(other_id.value().text(), std::string_view{"ZZZZ-0000000001"});
  EXPECT_NE(other_id.value().text().substr(0, 4), first.value().text().substr(0, 4));
}

TEST(OrderTransition, EveryTableRow) {
  const OrderView fresh{OrderState::IntentJournaled, 10, 0, RejectReason::None};
  OrderEvent local = event_kind(OrderEventKind::LocalReject);
  local.reject_reason = RejectReason::LocalValidation;
  EXPECT_TRUE(expect_move(fresh, event_kind(OrderEventKind::SentToBroker), OrderState::PendingNew, 0,
                          RejectReason::None));
  EXPECT_TRUE(expect_move(fresh, local, OrderState::Rejected, 0, RejectReason::LocalValidation));
  local.reject_reason = RejectReason::RiskGate;
  EXPECT_TRUE(expect_move(fresh, local, OrderState::Rejected, 0, RejectReason::RiskGate));

  const OrderView pending{OrderState::PendingNew, 10, 0, RejectReason::None};
  EXPECT_TRUE(expect_move(pending, event_kind(OrderEventKind::BrokerAck), OrderState::Acked, 0,
                          RejectReason::None));
  EXPECT_TRUE(expect_move(pending, event_kind(OrderEventKind::BrokerDefinitiveReject),
                          OrderState::Rejected, 0, RejectReason::Broker));
  EXPECT_TRUE(expect_move(pending, event_kind(OrderEventKind::TimeoutOrAmbiguous), OrderState::Unknown,
                          0, RejectReason::None));

  const OrderView acked{OrderState::Acked, 10, 0, RejectReason::None};
  OrderEvent partial = event_kind(OrderEventKind::Fill);
  partial.fill_qty_lots = 4;
  partial.fill_id = 1;
  EXPECT_TRUE(expect_move(acked, partial, OrderState::PartiallyFilled, 4, RejectReason::None));
  OrderEvent full = partial;
  full.fill_qty_lots = 10;
  EXPECT_TRUE(expect_move(acked, full, OrderState::Filled, 10, RejectReason::None));
  EXPECT_TRUE(expect_move(acked, event_kind(OrderEventKind::CancelRequested), OrderState::PendingCancel,
                          0, RejectReason::None));
  EXPECT_TRUE(expect_move(acked, event_kind(OrderEventKind::ExchangeCancel), OrderState::Cancelled, 0,
                          RejectReason::None));
  EXPECT_TRUE(expect_move(acked, event_kind(OrderEventKind::Expiry), OrderState::Expired, 0,
                          RejectReason::None));

  const OrderView part{OrderState::PartiallyFilled, 10, 4, RejectReason::None};
  OrderEvent rest = event_kind(OrderEventKind::Fill);
  rest.fill_qty_lots = 6;
  EXPECT_TRUE(expect_move(part, rest, OrderState::Filled, 10, RejectReason::None));
  OrderEvent more = rest;
  more.fill_qty_lots = 1;
  EXPECT_TRUE(expect_move(part, more, OrderState::PartiallyFilled, 5, RejectReason::None));
  EXPECT_TRUE(expect_move(part, event_kind(OrderEventKind::CancelRequested), OrderState::PendingCancel, 4,
                          RejectReason::None));
  EXPECT_TRUE(expect_move(part, event_kind(OrderEventKind::ExchangeCancel), OrderState::Cancelled, 4,
                          RejectReason::None));
  EXPECT_TRUE(expect_move(part, event_kind(OrderEventKind::Expiry), OrderState::Expired, 4,
                          RejectReason::None));

  const OrderView pending_cancel{OrderState::PendingCancel, 10, 0, RejectReason::None};
  EXPECT_TRUE(expect_move(pending_cancel, event_kind(OrderEventKind::CancelConfirmed),
                          OrderState::Cancelled, 0, RejectReason::None));
  OrderEvent fill_before = event_kind(OrderEventKind::Fill);
  fill_before.fill_qty_lots = 4;
  EXPECT_TRUE(expect_move(pending_cancel, fill_before, OrderState::PartiallyFilled, 4, RejectReason::None));
  fill_before.fill_qty_lots = 10;
  EXPECT_TRUE(expect_move(pending_cancel, fill_before, OrderState::Filled, 10, RejectReason::None));
  const OrderView pending_cancel_part{OrderState::PendingCancel, 10, 4, RejectReason::None};
  OrderEvent finish = event_kind(OrderEventKind::Fill);
  finish.fill_qty_lots = 6;
  EXPECT_TRUE(expect_move(pending_cancel_part, finish, OrderState::Filled, 10, RejectReason::None));

  const OrderView unknown{OrderState::Unknown, 10, 0, RejectReason::None};
  OrderEvent found = event_kind(OrderEventKind::QueryFound);
  found.queried_state = OrderState::Acked;
  EXPECT_TRUE(expect_move(unknown, found, OrderState::Acked, 0, RejectReason::None));
  found.queried_state = OrderState::Filled;
  EXPECT_TRUE(expect_move(unknown, found, OrderState::Filled, 10, RejectReason::None));
  found.queried_state = OrderState::PartiallyFilled;
  found.reported_filled_qty = 3;
  EXPECT_TRUE(expect_move(unknown, found, OrderState::PartiallyFilled, 3, RejectReason::None));
  found.queried_state = OrderState::Rejected;
  EXPECT_TRUE(expect_move(unknown, found, OrderState::Rejected, 0, RejectReason::Broker));
  EXPECT_TRUE(expect_move(unknown, event_kind(OrderEventKind::QueryNotFoundAfterRetry), OrderState::Rejected,
                          0, RejectReason::NotFoundAfterQuery));
}

TEST(OrderTransition, DuplicateFillDoesNotMove) {
  const OrderView part{OrderState::PartiallyFilled, 10, 4, RejectReason::None};
  OrderEvent again = event_kind(OrderEventKind::Fill);
  again.fill_qty_lots = 4;
  again.fill_id = 5;
  const auto result = apply_transition(part, again, true);
  ASSERT_TRUE(result);
  EXPECT_TRUE(result.value().duplicate_fill);
  EXPECT_EQ(result.value().state, OrderState::PartiallyFilled);
  EXPECT_EQ(result.value().filled_qty_lots, 4);
}

TEST(OrderTransition, MalformedEventsAreInvalid) {
  const OrderView acked{OrderState::Acked, 10, 0, RejectReason::None};
  OrderEvent zero = event_kind(OrderEventKind::Fill);
  zero.fill_qty_lots = 0;
  EXPECT_FALSE(apply_transition(acked, zero, false));
  OrderEvent over = zero;
  over.fill_qty_lots = 11;
  EXPECT_EQ(apply_transition(acked, over, false).error(), OrderError::InvalidTransition);

  const OrderView fresh{OrderState::IntentJournaled, 10, 0, RejectReason::None};
  OrderEvent bare = event_kind(OrderEventKind::LocalReject);
  EXPECT_FALSE(apply_transition(fresh, bare, false));

  const OrderView unknown{OrderState::Unknown, 10, 0, RejectReason::None};
  OrderEvent bad_query = event_kind(OrderEventKind::QueryFound);
  bad_query.queried_state = OrderState::Unknown;
  EXPECT_FALSE(apply_transition(unknown, bad_query, false));
  bad_query.queried_state = OrderState::IntentJournaled;
  EXPECT_FALSE(apply_transition(unknown, bad_query, false));
  bad_query.queried_state = OrderState::PartiallyFilled;
  bad_query.reported_filled_qty = 0;
  EXPECT_FALSE(apply_transition(unknown, bad_query, false));
  bad_query.reported_filled_qty = 10;
  EXPECT_FALSE(apply_transition(unknown, bad_query, false));
  bad_query.queried_state = OrderState::Filled;
  bad_query.reported_filled_qty = 4;
  EXPECT_FALSE(apply_transition(unknown, bad_query, false));

  const OrderView one_lot{OrderState::Unknown, 1, 0, RejectReason::None};
  OrderEvent partial_one = event_kind(OrderEventKind::QueryFound);
  partial_one.queried_state = OrderState::PartiallyFilled;
  partial_one.reported_filled_qty = 1;
  EXPECT_FALSE(apply_transition(one_lot, partial_one, false));
}

TEST(OrderTransition, RandomSequencesStayConsistent) {
  std::mt19937 rng{20261101};
  std::uniform_int_distribution<int> kind_dist(1, 12);
  std::uniform_int_distribution<int> small(0, 12);
  constexpr int kOrders = 10000;
  constexpr int kSteps = 24;
  for (int n = 0; n < kOrders; ++n) {
    OrderView view{OrderState::IntentJournaled, 10, 0, RejectReason::None};
    std::vector<std::uint64_t> seen;
    for (int step = 0; step < kSteps; ++step) {
      OrderEvent event;
      event.kind = static_cast<OrderEventKind>(kind_dist(rng));
      event.fill_qty_lots = small(rng);
      event.fill_id = static_cast<std::uint64_t>(small(rng));
      event.queried_state = static_cast<OrderState>(1 + (kind_dist(rng) % 10));
      event.reported_filled_qty = small(rng);
      event.reject_reason = static_cast<RejectReason>(small(rng) % 5);
      bool duplicate = false;
      if (event.kind == OrderEventKind::Fill && event.fill_id != 0) {
        for (const std::uint64_t id : seen) {
          if (id == event.fill_id) {
            duplicate = true;
          }
        }
      }
      const OrderView before = view;
      const auto result = apply_transition(view, event, duplicate);
      if (!result) {
        EXPECT_EQ(result.error(), OrderError::InvalidTransition);
        EXPECT_EQ(view.state, before.state);
        EXPECT_EQ(view.filled_qty_lots, before.filled_qty_lots);
      } else if (result.value().duplicate_fill) {
        EXPECT_EQ(result.value().state, before.state);
        EXPECT_EQ(result.value().filled_qty_lots, before.filled_qty_lots);
      } else {
        if (event.kind == OrderEventKind::Fill) {
          EXPECT_EQ(result.value().filled_qty_lots, before.filled_qty_lots + event.fill_qty_lots);
          if (event.fill_id != 0) {
            seen.push_back(event.fill_id);
          }
        }
        view.state = result.value().state;
        view.filled_qty_lots = result.value().filled_qty_lots;
        view.reject_reason = result.value().reject_reason;
      }
      EXPECT_GE(view.filled_qty_lots, 0);
      EXPECT_LE(view.filled_qty_lots, view.qty_lots);
      if (view.state == OrderState::Filled) {
        EXPECT_EQ(view.filled_qty_lots, view.qty_lots);
      }
      if (view.state == OrderState::PartiallyFilled) {
        EXPECT_GT(view.filled_qty_lots, 0);
        EXPECT_LT(view.filled_qty_lots, view.qty_lots);
      }
      if (is_terminal(before.state)) {
        EXPECT_EQ(view.state, before.state);
        EXPECT_EQ(view.filled_qty_lots, before.filled_qty_lots);
      }
    }
    if (!is_terminal(view.state) && view.state != OrderState::Unknown) {
      OrderEvent finish;
      switch (view.state) {
      case OrderState::IntentJournaled:
        finish.kind = OrderEventKind::LocalReject;
        finish.reject_reason = RejectReason::LocalValidation;
        break;
      case OrderState::PendingNew:
        finish.kind = OrderEventKind::BrokerDefinitiveReject;
        break;
      case OrderState::Acked:
      case OrderState::PartiallyFilled:
        finish.kind = OrderEventKind::Expiry;
        break;
      case OrderState::PendingCancel:
        finish.kind = OrderEventKind::CancelConfirmed;
        break;
      case OrderState::Filled:
      case OrderState::Cancelled:
      case OrderState::Rejected:
      case OrderState::Expired:
      case OrderState::Unknown:
        break;
      }
      const auto closed = apply_transition(view, finish, false);
      ASSERT_TRUE(closed);
      view.state = closed.value().state;
      view.filled_qty_lots = closed.value().filled_qty_lots;
    }
    EXPECT_TRUE(is_terminal(view.state) || view.state == OrderState::Unknown);
  }
}

TEST(OrderBook, DurableIntentBeforeSend) {
  Harness harness;
  harness.journal.accept_intent = false;
  auto denied = harness.book.open_intent(sample());
  EXPECT_FALSE(denied);
  EXPECT_EQ(denied.error(), OrderError::JournalFailed);
  EXPECT_EQ(harness.sends.news, 0);
  EXPECT_EQ(harness.book.order_count(), 0U);
  EXPECT_EQ(harness.journal.intents, 0);
  ASSERT_EQ(harness.journal.counters.size(), 1U);
  EXPECT_EQ(harness.journal.counters[0], 1U);

  harness.journal.accept_intent = true;
  auto opened = harness.book.open_intent(sample());
  ASSERT_TRUE(opened);
  EXPECT_EQ(opened.value().text(), std::string_view{"S7F2-0000000002"});
  EXPECT_EQ(harness.sends.news, 0);
  EXPECT_EQ(harness.book.find(opened.value())->state, OrderState::IntentJournaled);
  EXPECT_TRUE(harness.book.find(opened.value())->intent_durable);

  harness.journal.accept_transition = false;
  auto unsent = harness.book.send(opened.value());
  EXPECT_FALSE(unsent);
  EXPECT_EQ(unsent.error(), OrderError::JournalFailed);
  EXPECT_EQ(harness.sends.news, 0);
  EXPECT_EQ(harness.book.find(opened.value())->state, OrderState::IntentJournaled);

  harness.journal.accept_transition = true;
  auto sent = harness.book.send(opened.value());
  ASSERT_TRUE(sent);
  EXPECT_EQ(sent.value(), OrderState::PendingNew);
  EXPECT_EQ(harness.sends.news, 1);
  EXPECT_EQ(harness.sends.last_new, opened.value());
  EXPECT_EQ(harness.sends.last_request.qty_lots, 10);
  EXPECT_EQ(harness.sends.last_request.price_ticks, 100);
  ASSERT_EQ(harness.journal.transitions.size(), 1U);
  EXPECT_EQ(harness.journal.transitions[0].from, OrderState::IntentJournaled);
  EXPECT_EQ(harness.journal.transitions[0].to, OrderState::PendingNew);
}

TEST(OrderBook, InvalidQuantityDoesNotJournalOrSend) {
  Harness harness;
  auto zero = sample();
  zero.qty_lots = 0;
  auto opened = harness.book.open_intent(zero);
  EXPECT_FALSE(opened);
  EXPECT_EQ(opened.error(), OrderError::InvalidQuantity);
  EXPECT_EQ(harness.ids.next_counter(), 1U);
  EXPECT_EQ(harness.sends.news, 0);
  EXPECT_EQ(harness.journal.intents, 0);

  auto negative = sample();
  negative.qty_lots = -1;
  EXPECT_EQ(harness.book.open_intent(negative).error(), OrderError::InvalidQuantity);
  EXPECT_EQ(harness.ids.next_counter(), 1U);
}

TEST(OrderBook, DuplicateClientIdIsRejected) {
  auto first = format_client_order_id("S7F2", 1);
  ASSERT_TRUE(first);
  ScriptedIds ids({first.value(), first.value()});
  JournalProbe journal;
  SendProbe sends;
  OrderBook book(ids, journal_port(&journal), send_port(&sends));
  ASSERT_TRUE(book.open_intent(sample()));
  auto again = book.open_intent(sample());
  EXPECT_FALSE(again);
  EXPECT_EQ(again.error(), OrderError::DuplicateIntent);
  EXPECT_EQ(book.order_count(), 1U);
  EXPECT_EQ(journal.intents, 1);
  EXPECT_EQ(sends.news, 0);
}

TEST(OrderBook, LifecycleIsJournaled) {
  Harness harness;
  const ClientOrderId id = open_sample(harness);
  ASSERT_FALSE(id.empty());
  ASSERT_TRUE(harness.book.send(id));
  ASSERT_TRUE(harness.book.apply(id, event_kind(OrderEventKind::BrokerAck)));
  OrderEvent partial = event_kind(OrderEventKind::Fill);
  partial.fill_qty_lots = 4;
  partial.fill_id = 7;
  ASSERT_TRUE(harness.book.apply(id, partial));
  OrderEvent rest = partial;
  rest.fill_qty_lots = 6;
  rest.fill_id = 8;
  ASSERT_TRUE(harness.book.apply(id, rest));

  const auto* order = harness.book.find(id);
  ASSERT_NE(order, nullptr);
  EXPECT_EQ(order->state, OrderState::Filled);
  EXPECT_EQ(order->filled_qty_lots, 10);
  EXPECT_EQ(harness.sends.news, 1);
  EXPECT_EQ(harness.sends.cancels, 0);
  ASSERT_EQ(harness.journal.transitions.size(), 4U);
  EXPECT_EQ(harness.journal.transitions[0].to, OrderState::PendingNew);
  EXPECT_EQ(harness.journal.transitions[1].to, OrderState::Acked);
  EXPECT_EQ(harness.journal.transitions[2].to, OrderState::PartiallyFilled);
  EXPECT_EQ(harness.journal.transitions[3].to, OrderState::Filled);

  OrderEvent duplicate = partial;
  duplicate.fill_qty_lots = 4;
  const auto ignored = harness.book.apply(id, duplicate);
  ASSERT_TRUE(ignored);
  EXPECT_EQ(harness.book.find(id)->duplicate_fill_count, 1U);
  EXPECT_EQ(harness.book.find(id)->filled_qty_lots, 10);
  EXPECT_EQ(harness.journal.transitions.size(), 4U);
  EXPECT_TRUE(harness.book.defects().empty());
}

TEST(OrderBook, FillBeforeCancelDoesNotBecomeCancelled) {
  Harness harness;
  const ClientOrderId id = open_sample(harness);
  ASSERT_TRUE(harness.book.send(id));
  ASSERT_TRUE(harness.book.apply(id, event_kind(OrderEventKind::BrokerAck)));
  ASSERT_TRUE(harness.book.apply(id, event_kind(OrderEventKind::CancelRequested)));
  EXPECT_EQ(harness.sends.cancels, 1);
  EXPECT_EQ(harness.sends.last_cancel, id);

  OrderEvent partial = event_kind(OrderEventKind::Fill);
  partial.fill_qty_lots = 4;
  partial.fill_id = 3;
  ASSERT_TRUE(harness.book.apply(id, partial));
  EXPECT_EQ(harness.book.find(id)->state, OrderState::PartiallyFilled);
  EXPECT_EQ(harness.book.find(id)->filled_qty_lots, 4);

  Harness full;
  const ClientOrderId full_id = open_sample(full);
  ASSERT_TRUE(full.book.send(full_id));
  ASSERT_TRUE(full.book.apply(full_id, event_kind(OrderEventKind::BrokerAck)));
  ASSERT_TRUE(full.book.apply(full_id, event_kind(OrderEventKind::CancelRequested)));
  OrderEvent all = event_kind(OrderEventKind::Fill);
  all.fill_qty_lots = 10;
  all.fill_id = 4;
  ASSERT_TRUE(full.book.apply(full_id, all));
  EXPECT_EQ(full.book.find(full_id)->state, OrderState::Filled);
  EXPECT_EQ(full.book.find(full_id)->filled_qty_lots, 10);

  Harness cancelled;
  const ClientOrderId cancel_id = open_sample(cancelled);
  ASSERT_TRUE(drive_to(cancelled, cancel_id, OrderState::PartiallyFilled));
  ASSERT_TRUE(cancelled.book.apply(cancel_id, event_kind(OrderEventKind::ExchangeCancel)));
  EXPECT_EQ(cancelled.book.find(cancel_id)->state, OrderState::Cancelled);
  EXPECT_EQ(cancelled.book.find(cancel_id)->filled_qty_lots, 4);
}

TEST(OrderBook, ZeroFillIdIsNotDeduped) {
  Harness harness;
  const ClientOrderId id = open_sample(harness);
  ASSERT_TRUE(drive_to(harness, id, OrderState::Acked));
  OrderEvent first = event_kind(OrderEventKind::Fill);
  first.fill_qty_lots = 3;
  first.fill_id = 0;
  OrderEvent second = first;
  ASSERT_TRUE(harness.book.apply(id, first));
  ASSERT_TRUE(harness.book.apply(id, second));
  EXPECT_EQ(harness.book.find(id)->filled_qty_lots, 6);
  EXPECT_EQ(harness.book.find(id)->duplicate_fill_count, 0U);
}

TEST(OrderBook, OverfillHaltsScope) {
  Harness harness;
  const ClientOrderId id = open_sample(harness);
  ASSERT_TRUE(drive_to(harness, id, OrderState::Acked));
  OrderEvent over = event_kind(OrderEventKind::Fill);
  over.fill_qty_lots = 11;
  over.fill_id = 9;
  auto rejected = harness.book.apply(id, over);
  EXPECT_FALSE(rejected);
  EXPECT_EQ(rejected.error(), OrderError::InvalidTransition);
  EXPECT_EQ(harness.book.find(id)->state, OrderState::Acked);
  EXPECT_EQ(harness.book.find(id)->filled_qty_lots, 0);
  ASSERT_EQ(harness.book.defects().size(), 1U);
  EXPECT_TRUE(harness.book.scope_halted(7, 11));
  EXPECT_EQ(harness.sends.news, 1);

  auto same = harness.book.open_intent(sample());
  EXPECT_EQ(same.error(), OrderError::ScopeHalted);
  auto other = harness.book.open_intent(sample(7, 12));
  EXPECT_TRUE(other);
  auto other_strategy = harness.book.open_intent(sample(8, 11));
  EXPECT_TRUE(other_strategy);
}

TEST(OrderBook, InvalidPairsRejectLogDefectAndHalt) {
  int skipped = 0;
  int invalid_cases = 0;
  for (const OrderState state : kStates) {
    for (const OrderEventKind kind : kKinds) {
      if (table_allows(state, kind)) {
        ++skipped;
        continue;
      }
      ++invalid_cases;
      Harness harness;
      const ClientOrderId id = open_sample(harness);
      ASSERT_FALSE(id.empty());
      ASSERT_TRUE(drive_to(harness, id, state)) << "drive failed";
      const auto before_state = harness.book.find(id)->state;
      const auto before_filled = harness.book.find(id)->filled_qty_lots;
      const int news = harness.sends.news;
      const int cancels = harness.sends.cancels;
      auto rejected = harness.book.apply(id, canonical(kind));
      EXPECT_FALSE(rejected);
      if (!rejected) {
        EXPECT_EQ(rejected.error(), OrderError::InvalidTransition);
      }
      EXPECT_EQ(harness.book.find(id)->state, before_state);
      EXPECT_EQ(harness.book.find(id)->filled_qty_lots, before_filled);
      ASSERT_EQ(harness.book.defects().size(), 1U);
      EXPECT_EQ(harness.book.defects()[0].from, before_state);
      EXPECT_EQ(harness.book.defects()[0].event, kind);
      EXPECT_TRUE(harness.book.scope_halted(7, 11));
      ASSERT_FALSE(harness.book.logs().empty());
      EXPECT_TRUE(harness.book.logs().back().rejected);
      EXPECT_EQ(harness.book.logs().back().from, before_state);
      EXPECT_EQ(harness.book.logs().back().event, kind);
      EXPECT_STREQ(harness.book.logs().back().message, "invalid transition");
      EXPECT_EQ(harness.sends.news, news);
      EXPECT_EQ(harness.sends.cancels, cancels);
    }
  }
  EXPECT_EQ(skipped, 17);
  EXPECT_EQ(invalid_cases, 103);
}

TEST(OrderBook, UnknownBlocksNewOrdersUntilResolved) {
  Harness harness;
  const ClientOrderId resting = open_sample(harness);
  const ClientOrderId pending = open_sample(harness, sample());
  const ClientOrderId sibling = open_sample(harness);
  ASSERT_FALSE(sibling.empty());
  ASSERT_TRUE(harness.book.send(resting));
  ASSERT_TRUE(harness.book.apply(resting, event_kind(OrderEventKind::BrokerAck)));
  ASSERT_TRUE(harness.book.send(pending));
  ASSERT_TRUE(harness.book.apply(pending, event_kind(OrderEventKind::TimeoutOrAmbiguous)));
  EXPECT_TRUE(harness.book.scope_blocked(7, 11));
  EXPECT_FALSE(harness.book.scope_halted(7, 11));

  const auto counter = harness.ids.next_counter();
  auto blocked = harness.book.open_intent(sample());
  EXPECT_EQ(blocked.error(), OrderError::ScopeBlocked);
  EXPECT_EQ(harness.ids.next_counter(), counter);
  EXPECT_TRUE(harness.book.open_intent(sample(7, 12)));
  EXPECT_TRUE(harness.book.open_intent(sample(8, 11)));

  auto sibling_send = harness.book.send(sibling);
  EXPECT_EQ(sibling_send.error(), OrderError::ScopeBlocked);
  EXPECT_EQ(harness.book.find(sibling)->state, OrderState::IntentJournaled);

  OrderEvent fill = event_kind(OrderEventKind::Fill);
  fill.fill_qty_lots = 2;
  fill.fill_id = 4;
  EXPECT_TRUE(harness.book.apply(resting, fill));
  EXPECT_EQ(harness.book.find(resting)->state, OrderState::PartiallyFilled);
}

TEST(OrderBook, ResolveUnknownFollowsRetryRule) {
  Harness found_first;
  const ClientOrderId first_id = open_sample(found_first);
  ASSERT_TRUE(drive_to(found_first, first_id, OrderState::Unknown));
  LookupProbe first_lookup;
  QueryResponse found;
  found.status = QueryResponse::Status::Found;
  found.state = OrderState::Acked;
  first_lookup.script.push_back(found);
  const auto counter = found_first.ids.next_counter();
  const int news = found_first.sends.news;
  auto resolved = found_first.book.resolve_unknown(first_id, lookup_port(&first_lookup),
                                                   delay_port(&first_lookup));
  ASSERT_TRUE(resolved);
  EXPECT_EQ(resolved.value(), OrderState::Acked);
  EXPECT_EQ(first_lookup.queries, 1);
  EXPECT_TRUE(first_lookup.delays.empty());
  EXPECT_EQ(first_lookup.resends, 0);
  EXPECT_EQ(first_lookup.query_id, std::string(first_id.text()));
  EXPECT_FALSE(found_first.book.scope_blocked(7, 11));
  EXPECT_EQ(found_first.ids.next_counter(), counter);
  EXPECT_EQ(found_first.sends.news, news);
  EXPECT_TRUE(found_first.book.open_intent(sample()));

  Harness found_second;
  const ClientOrderId second_id = open_sample(found_second);
  ASSERT_TRUE(drive_to(found_second, second_id, OrderState::Unknown));
  LookupProbe second_lookup;
  QueryResponse missing;
  missing.status = QueryResponse::Status::NotFound;
  QueryResponse pending_new = found;
  pending_new.state = OrderState::PendingNew;
  second_lookup.script = {missing, pending_new};
  ASSERT_TRUE(found_second.book.resolve_unknown(second_id, lookup_port(&second_lookup),
                                                delay_port(&second_lookup)));
  EXPECT_EQ(found_second.book.find(second_id)->state, OrderState::PendingNew);
  EXPECT_EQ(second_lookup.queries, 2);
  ASSERT_EQ(second_lookup.delays.size(), 1U);
  EXPECT_EQ(second_lookup.delays[0], 1000U);
  EXPECT_EQ(second_lookup.resends, 0);

  Harness found_third;
  const ClientOrderId third_id = open_sample(found_third);
  ASSERT_TRUE(drive_to(found_third, third_id, OrderState::Unknown));
  LookupProbe third_lookup;
  QueryResponse filled = found;
  filled.state = OrderState::Filled;
  third_lookup.script = {missing, missing, filled};
  ASSERT_TRUE(found_third.book.resolve_unknown(third_id, lookup_port(&third_lookup),
                                               delay_port(&third_lookup)));
  EXPECT_EQ(found_third.book.find(third_id)->state, OrderState::Filled);
  EXPECT_EQ(found_third.book.find(third_id)->filled_qty_lots, 10);
  EXPECT_EQ(third_lookup.queries, 3);
  ASSERT_EQ(third_lookup.delays.size(), 2U);
  EXPECT_EQ(third_lookup.delays[0], 1000U);
  EXPECT_EQ(third_lookup.delays[1], 3000U);
  EXPECT_EQ(third_lookup.resends, 0);

  Harness resend_ok;
  const ClientOrderId resend_id = open_sample(resend_ok);
  ASSERT_TRUE(drive_to(resend_ok, resend_id, OrderState::Unknown));
  LookupProbe resend_lookup;
  resend_lookup.script = {missing, missing, missing};
  resend_lookup.resend.status = ResendResponse::Status::Resolved;
  resend_lookup.resend.state = OrderState::Acked;
  const auto resend_counter = resend_ok.ids.next_counter();
  const int resend_news = resend_ok.sends.news;
  ASSERT_TRUE(resend_ok.book.resolve_unknown(resend_id, lookup_port(&resend_lookup),
                                             delay_port(&resend_lookup)));
  EXPECT_EQ(resend_ok.book.find(resend_id)->state, OrderState::Acked);
  EXPECT_EQ(resend_lookup.resends, 1);
  EXPECT_EQ(resend_lookup.resend_id, std::string(resend_id.text()));
  EXPECT_EQ(resend_lookup.resend_request.qty_lots, 10);
  EXPECT_EQ(resend_lookup.resend_request.price_ticks, 100);
  EXPECT_EQ(resend_lookup.resend_request.side, Side::Buy);
  EXPECT_EQ(resend_lookup.resend_request.strategy_id, 7U);
  EXPECT_EQ(resend_lookup.resend_request.instrument_id, 11U);
  EXPECT_EQ(resend_ok.ids.next_counter(), resend_counter);
  EXPECT_EQ(resend_ok.sends.news, resend_news);
  EXPECT_FALSE(resend_ok.book.scope_blocked(7, 11));

  Harness not_found;
  const ClientOrderId missing_id = open_sample(not_found);
  ASSERT_TRUE(drive_to(not_found, missing_id, OrderState::Unknown));
  LookupProbe not_found_lookup;
  not_found_lookup.script = {missing, missing, missing};
  not_found_lookup.resend.status = ResendResponse::Status::NotFound;
  ASSERT_TRUE(not_found.book.resolve_unknown(missing_id, lookup_port(&not_found_lookup),
                                             delay_port(&not_found_lookup)));
  EXPECT_EQ(not_found.book.find(missing_id)->state, OrderState::Rejected);
  EXPECT_EQ(not_found.book.find(missing_id)->reject_reason, RejectReason::NotFoundAfterQuery);
  EXPECT_EQ(not_found_lookup.resends, 1);
  EXPECT_EQ(not_found_lookup.resend_id, std::string(missing_id.text()));
  EXPECT_FALSE(not_found.book.scope_blocked(7, 11));
  EXPECT_TRUE(not_found.book.alerts().empty());
  EXPECT_FALSE(not_found.book.scope_halted(7, 11));

  Harness unresolved;
  const ClientOrderId unresolved_id = open_sample(unresolved);
  ASSERT_TRUE(drive_to(unresolved, unresolved_id, OrderState::Unknown));
  LookupProbe unresolved_lookup;
  unresolved_lookup.script = {missing, missing, missing};
  unresolved_lookup.resend.status = ResendResponse::Status::Unresolved;
  ASSERT_TRUE(unresolved.book.resolve_unknown(unresolved_id, lookup_port(&unresolved_lookup),
                                              delay_port(&unresolved_lookup)));
  EXPECT_EQ(unresolved.book.find(unresolved_id)->state, OrderState::Unknown);
  EXPECT_TRUE(unresolved.book.scope_blocked(7, 11));
  ASSERT_EQ(unresolved.book.alerts().size(), 1U);
  EXPECT_EQ(unresolved.book.alerts()[0].code, kAlertAl04);
  EXPECT_EQ(unresolved.book.alerts()[0].client_id, unresolved_id);
  EXPECT_EQ(unresolved_lookup.resends, 1);
  EXPECT_EQ(unresolved.ids.next_counter(), unresolved.ids.next_counter());
  auto second = unresolved.book.resolve_unknown(unresolved_id, lookup_port(&unresolved_lookup),
                                                delay_port(&unresolved_lookup));
  EXPECT_EQ(second.error(), OrderError::AwaitingOperator);
  EXPECT_EQ(unresolved_lookup.queries, 3);
  EXPECT_EQ(unresolved_lookup.resends, 1);
  EXPECT_EQ(unresolved.book.alerts().size(), 1U);
  EXPECT_EQ(unresolved.book.find(unresolved_id)->state, OrderState::Unknown);
  EXPECT_TRUE(unresolved.book.scope_blocked(7, 11));

  Harness bad_payload;
  const ClientOrderId bad_id = open_sample(bad_payload);
  ASSERT_TRUE(drive_to(bad_payload, bad_id, OrderState::Unknown));
  LookupProbe bad_lookup;
  QueryResponse bad_partial;
  bad_partial.status = QueryResponse::Status::Found;
  bad_partial.state = OrderState::PartiallyFilled;
  bad_partial.reported_filled_qty = 0;
  bad_lookup.script = {bad_partial, found};
  ASSERT_TRUE(bad_payload.book.resolve_unknown(bad_id, lookup_port(&bad_lookup), delay_port(&bad_lookup)));
  EXPECT_EQ(bad_payload.book.find(bad_id)->state, OrderState::Acked);
  EXPECT_EQ(bad_lookup.queries, 2);
  EXPECT_TRUE(bad_payload.book.defects().empty());
  EXPECT_FALSE(bad_payload.book.scope_halted(7, 11));

  Harness acked;
  const ClientOrderId acked_id = open_sample(acked);
  ASSERT_TRUE(drive_to(acked, acked_id, OrderState::Acked));
  LookupProbe unused;
  auto not_unknown = acked.book.resolve_unknown(acked_id, lookup_port(&unused), delay_port(&unused));
  EXPECT_EQ(not_unknown.error(), OrderError::NotUnknown);
  EXPECT_EQ(unused.queries, 0);
  EXPECT_EQ(unused.resends, 0);

  auto missing_order = acked.book.resolve_unknown(ClientOrderId{}, lookup_port(&unused), delay_port(&unused));
  EXPECT_EQ(missing_order.error(), OrderError::UnknownClientId);
}

TEST(OrderBook, ResolvingOneUnknownKeepsAnotherBlocked) {
  Harness harness;
  const ClientOrderId first = open_sample(harness);
  const ClientOrderId second = open_sample(harness);
  ASSERT_TRUE(harness.book.send(first));
  ASSERT_TRUE(harness.book.send(second));
  ASSERT_TRUE(harness.book.apply(first, event_kind(OrderEventKind::TimeoutOrAmbiguous)));
  ASSERT_TRUE(harness.book.apply(second, event_kind(OrderEventKind::TimeoutOrAmbiguous)));

  LookupProbe lookup;
  QueryResponse found;
  found.status = QueryResponse::Status::Found;
  found.state = OrderState::Acked;
  lookup.script = {found};
  ASSERT_TRUE(harness.book.resolve_unknown(first, lookup_port(&lookup), delay_port(&lookup)));
  EXPECT_EQ(harness.book.find(first)->state, OrderState::Acked);
  EXPECT_TRUE(harness.book.scope_blocked(7, 11));

  LookupProbe second_lookup;
  second_lookup.script = {found};
  ASSERT_TRUE(harness.book.resolve_unknown(second, lookup_port(&second_lookup), delay_port(&second_lookup)));
  EXPECT_EQ(harness.book.find(second)->state, OrderState::Acked);
  EXPECT_FALSE(harness.book.scope_blocked(7, 11));
}

TEST(OrderBook, RiskRejectDoesNotSend) {
  Harness harness;
  const ClientOrderId id = open_sample(harness);
  OrderEvent reject = event_kind(OrderEventKind::LocalReject);
  reject.reject_reason = RejectReason::RiskGate;
  ASSERT_TRUE(harness.book.apply(id, reject));
  EXPECT_EQ(harness.book.find(id)->state, OrderState::Rejected);
  EXPECT_EQ(harness.book.find(id)->reject_reason, RejectReason::RiskGate);
  EXPECT_EQ(harness.sends.news, 0);
  EXPECT_EQ(harness.journal.transitions.size(), 1U);
  EXPECT_EQ(harness.journal.transitions[0].to, OrderState::Rejected);
}
