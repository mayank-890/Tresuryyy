#include "trading/orders/client_order_id.hpp"

#include <cstring>

namespace trading::orders {

namespace {

constexpr char kBase36[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr std::uint32_t kPrefixRadix = 36;
constexpr std::uint32_t kPrefixSpace = 36U * 36U * 36U * 36U;

[[nodiscard]] bool prefix_char(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
}

[[nodiscard]] bool prefix_ok(std::string_view prefix) {
  if (prefix.size() != kSessionPrefixLen) {
    return false;
  }
  for (const char c : prefix) {
    if (!prefix_char(c)) {
      return false;
    }
  }
  return true;
}

} // namespace

core::Result<std::array<char, kSessionPrefixLen>, OrderError>
session_prefix_from_counter(std::uint32_t counter) {
  if (counter >= kPrefixSpace) {
    return core::Result<std::array<char, kSessionPrefixLen>, OrderError>::err(
        OrderError::CounterExhausted);
  }
  std::array<char, kSessionPrefixLen> out{};
  for (int i = static_cast<int>(kSessionPrefixLen) - 1; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = kBase36[counter % kPrefixRadix];
    counter /= kPrefixRadix;
  }
  return core::Result<std::array<char, kSessionPrefixLen>, OrderError>::ok(out);
}

core::Result<ClientOrderId, OrderError> format_client_order_id(std::string_view prefix,
                                                               std::uint64_t counter) {
  if (!prefix_ok(prefix)) {
    return core::Result<ClientOrderId, OrderError>::err(OrderError::BadPrefix);
  }
  if (counter > kMaxClientCounter) {
    return core::Result<ClientOrderId, OrderError>::err(OrderError::CounterExhausted);
  }
  ClientOrderId id{};
  std::memcpy(id.bytes.data(), prefix.data(), kSessionPrefixLen);
  id.bytes[kSessionPrefixLen] = '-';
  for (int i = static_cast<int>(kClientIdCounterDigits) - 1; i >= 0; --i) {
    id.bytes[kSessionPrefixLen + 1 + static_cast<std::size_t>(i)] =
        static_cast<char>('0' + static_cast<int>(counter % 10));
    counter /= 10;
  }
  return core::Result<ClientOrderId, OrderError>::ok(id);
}

ClientOrderIdGenerator::ClientOrderIdGenerator(std::string_view prefix, std::uint64_t next_counter,
                                               PersistFn persist, void* ctx)
    : next_counter_(next_counter), persist_(persist), ctx_(ctx), bad_prefix_(!prefix_ok(prefix)) {
  if (!bad_prefix_) {
    std::memcpy(prefix_.data(), prefix.data(), kSessionPrefixLen);
  }
}

core::Result<ClientOrderId, OrderError> ClientOrderIdGenerator::next() {
  if (bad_prefix_) {
    return core::Result<ClientOrderId, OrderError>::err(OrderError::BadPrefix);
  }
  if (next_counter_ > kMaxClientCounter) {
    return core::Result<ClientOrderId, OrderError>::err(OrderError::CounterExhausted);
  }
  // The counter is durable before the id is observable, so a failed journal
  // does not consume it and a later success cannot reuse it.
  if (persist_ == nullptr || !persist_(ctx_, next_counter_)) {
    return core::Result<ClientOrderId, OrderError>::err(OrderError::JournalFailed);
  }
  auto formatted = format_client_order_id(prefix(), next_counter_);
  if (!formatted) {
    return formatted;
  }
  ++next_counter_;
  return formatted;
}

} // namespace trading::orders
