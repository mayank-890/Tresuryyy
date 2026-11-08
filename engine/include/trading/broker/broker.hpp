#pragma once

#include "trading/broker/types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace trading::broker {

// DESIGN §9.1 — strategies and execution use this interface only.
class IBroker {
public:
  virtual ~IBroker() = default;

  IBroker(const IBroker&) = delete;
  IBroker& operator=(const IBroker&) = delete;
  IBroker(IBroker&&) = delete;
  IBroker& operator=(IBroker&&) = delete;

  [[nodiscard]] virtual BrokerHealth health() const = 0;

  virtual bool connect() = 0;
  virtual void disconnect() = 0;

  [[nodiscard]] virtual SubmitResult place_order(const PlaceOrderRequest& request) = 0;
  [[nodiscard]] virtual CancelResult cancel_order(const orders::ClientOrderId& client_id) = 0;
  [[nodiscard]] virtual QueryResult query_order(const orders::ClientOrderId& client_id) = 0;

  [[nodiscard]] virtual std::vector<PrivateEvent> poll_private_events() = 0;
  virtual void tick(std::int64_t now_mono_ns) = 0;

protected:
  IBroker() = default;
};

} // namespace trading::broker
