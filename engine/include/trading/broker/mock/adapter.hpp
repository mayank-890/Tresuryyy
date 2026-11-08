#pragma once

#include "trading/broker/broker.hpp"
#include "trading/broker/mock/scenario.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace trading::broker::mock {

class MockExchangeBroker final : public IBroker {
public:
  MockExchangeBroker();
  explicit MockExchangeBroker(MockScenario scenario);

  [[nodiscard]] const MockScenario& scenario() const noexcept { return scenario_; }
  [[nodiscard]] std::uint32_t place_calls() const noexcept { return place_calls_; }

  [[nodiscard]] BrokerHealth health() const override;

  bool connect() override;
  void disconnect() override;

  [[nodiscard]] SubmitResult place_order(const PlaceOrderRequest& request) override;
  [[nodiscard]] CancelResult cancel_order(const orders::ClientOrderId& client_id) override;
  [[nodiscard]] QueryResult query_order(const orders::ClientOrderId& client_id) override;

  [[nodiscard]] std::vector<PrivateEvent> poll_private_events() override;
  void tick(std::int64_t now_mono_ns) override;

private:
  struct StoredMockOrder {
    PlaceOrderRequest request{};
    std::uint64_t broker_order_id{0};
    std::int32_t filled_qty_lots{0};
    orders::OrderState state{orders::OrderState::Acked};
    bool cancelled{false};
  };

  struct PendingEvent {
    std::int64_t fire_at_mono_ns{0};
    PrivateEvent event{};
    orders::ClientOrderId client_id{};
    std::int32_t fill_qty_lots{0};
  };

  [[nodiscard]] const PlaceStep* next_place_step() noexcept;
  void schedule_fills(const orders::ClientOrderId& client_id, const PlaceOrderRequest& request,
                      const PlaceStep& step, std::int64_t place_mono_ns);
  void apply_timeline(std::int64_t now_mono_ns);
  void fire_due_events(std::int64_t now_mono_ns);
  [[nodiscard]] StoredMockOrder* find_order(const orders::ClientOrderId& client_id) noexcept;

  MockScenario scenario_{};
  bool session_connected_{false};
  bool link_up_{true};
  std::uint32_t place_calls_{0};
  std::size_t place_step_index_{0};
  std::uint64_t next_auto_broker_id_{1};
  std::int64_t last_mono_ns_{0};
  std::map<std::string, StoredMockOrder> orders_{};
  std::deque<PrivateEvent> pending_events_{};
  std::vector<PendingEvent> scheduled_{};
  std::size_t timeline_index_{0};
};

} // namespace trading::broker::mock
