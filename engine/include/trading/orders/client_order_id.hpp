#pragma once

#include "trading/core/result.hpp"
#include "trading/orders/types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace trading::orders {

// 4-character base-36 prefix from the persisted session counter (DESIGN §5.3).
[[nodiscard]] core::Result<std::array<char, kSessionPrefixLen>, OrderError>
session_prefix_from_counter(std::uint32_t counter);

// `<prefix>-<10-digit counter>`. Does not journal and does not advance a counter.
[[nodiscard]] core::Result<ClientOrderId, OrderError>
format_client_order_id(std::string_view prefix, std::uint64_t counter);

// Issues the next id only after `persist` reports the counter durable.
class IdSource {
public:
  virtual ~IdSource() = default;
  [[nodiscard]] virtual core::Result<ClientOrderId, OrderError> next() = 0;

protected:
  IdSource() = default;
  IdSource(const IdSource&) = delete;
  IdSource& operator=(const IdSource&) = delete;
  IdSource(IdSource&&) = delete;
  IdSource& operator=(IdSource&&) = delete;
};

class ClientOrderIdGenerator final : public IdSource {
public:
  using PersistFn = bool (*)(void* ctx, std::uint64_t counter);

  ClientOrderIdGenerator(std::string_view prefix, std::uint64_t next_counter, PersistFn persist,
                         void* ctx);

  [[nodiscard]] core::Result<ClientOrderId, OrderError> next() override;
  [[nodiscard]] std::uint64_t next_counter() const { return next_counter_; }
  [[nodiscard]] std::string_view prefix() const {
    return std::string_view(prefix_.data(), prefix_.size());
  }

private:
  std::array<char, kSessionPrefixLen> prefix_{};
  std::uint64_t next_counter_{0};
  PersistFn persist_{nullptr};
  void* ctx_{nullptr};
  bool bad_prefix_{false};
};

} // namespace trading::orders
