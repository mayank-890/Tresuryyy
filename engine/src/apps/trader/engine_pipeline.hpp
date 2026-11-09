#pragma once

#include "trading/core/spsc_ring.hpp"
#include "trading/marketdata/pipeline_types.hpp"
#include "trading/orders/client_order_id.hpp"
#include "broker_execution.hpp"

#include "trading/broker/broker.hpp"
#include "trading/orders/execution_sink.hpp"
#include "trading/risk/gate.hpp"
#include "trading/risk/pipeline_types.hpp"
#include "trading/strategy/pipeline_types.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace trading::strategy {
class IStrategy;
}

namespace trading::trader {

struct EngineThreadCores {
  int market_data{0};
  int strategy{1};
  int risk_gate{2};
  int execution{3};
  int journal{4};
};

struct EnginePipelineConfig {
  EngineThreadCores cores{};
  bool pin_threads{true};
};

// In-process skeleton for DESIGN §4 rings T1–T4 and T7. No broker I/O.
class EnginePipeline {
public:
  struct JournalDrain {
    void (*on_record)(void* ctx, const risk::DecisionRecord& record){nullptr};
    void* ctx{nullptr};
  };

  EnginePipeline(const strategy::IStrategy& strategy, risk::RiskGate& gate,
                 orders::ClientOrderIdGenerator& id_source, EnginePipelineConfig config = {});

  ~EnginePipeline();

  EnginePipeline(const EnginePipeline&) = delete;
  EnginePipeline& operator=(const EnginePipeline&) = delete;

  void set_portfolio_view(risk::PortfolioView view);
  void set_journal_drain(JournalDrain drain);
  void set_param_blob(const void* data, std::size_t size, std::uint64_t version);
  void set_broker(broker::IBroker* broker) noexcept {
    // Release: execution thread acquire-loads before use.
    broker_.store(broker, std::memory_order_release);
  }

  [[nodiscard]] const BrokerExecutionStats& broker_stats() const noexcept { return broker_stats_; }

  // Starts threads and runs warmup quotes through the full path.
  void start_and_warmup(std::uint32_t warmup_cycles = 3);

  // Producer API for tests and injectors. Returns false if the ingress ring is full.
  [[nodiscard]] bool submit_quote(const marketdata::QuoteIngress& quote) noexcept;

  [[nodiscard]] const orders::ExecutionSink& execution_sink() const noexcept { return sink_; }
  [[nodiscard]] std::uint32_t journal_drained() const noexcept {
    return journal_drained_.load(std::memory_order_acquire);
  }

  void request_stop() noexcept;
  void join();

  [[nodiscard]] bool running() const noexcept {
    return running_.load(std::memory_order_acquire);
  }

private:
  void run_market_data();
  void run_strategy();
  void run_risk_gate();
  void run_execution();
  void run_journal_writer();

  [[nodiscard]] risk::PortfolioView view_copy() const noexcept;

  const strategy::IStrategy& strategy_;
  risk::RiskGate& gate_;
  orders::ClientOrderIdGenerator& id_source_;
  EnginePipelineConfig config_;

  core::SpscRing<marketdata::QuoteIngress, 64> inject_to_md_{};
  core::SpscRing<marketdata::QuoteIngress, 64> md_to_strategy_{};
  core::SpscRing<strategy::IntentIngress, 64> strategy_to_risk_{};
  core::SpscRing<risk::ApprovedIngress, 64> risk_to_execution_{};
  core::SpscRing<risk::JournalIngress, 256> risk_to_journal_{};

  orders::ExecutionSink sink_{};
  JournalDrain journal_drain_{};
  risk::PortfolioView view_{};
  std::array<std::byte, 256> param_bytes_{};
  std::size_t param_size_{0};
  std::uint64_t param_version_{0};

  std::atomic<bool> stop_{false};
  std::atomic<bool> running_{false};
  std::atomic<std::uint32_t> journal_drained_{0};

  std::atomic<broker::IBroker*> broker_{nullptr};
  BrokerExecutionStats broker_stats_{};

  std::thread t_market_data_;
  std::thread t_strategy_;
  std::thread t_risk_;
  std::thread t_execution_;
  std::thread t_journal_;
};

} // namespace trading::trader
