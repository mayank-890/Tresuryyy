#include "engine_pipeline.hpp"

#include "broker_execution.hpp"

#include "trading/core/clock.hpp"
#include "trading/core/object_pool.hpp"
#include "trading/core/thread_pin.hpp"
#include "trading/orders/client_order_id.hpp"
#include "trading/orders/execution_sink.hpp"
#include "trading/strategy/strategy.hpp"

#include <chrono>
#include <cstring>
#include <span>
#include <thread>

namespace trading::trader {

namespace {

constexpr std::uint32_t kParamStorageBytes = 256;

void pin_if_requested(int core, bool enable) {
  if (!enable) {
    return;
  }
  (void)core::pinThread(core);
}

[[nodiscard]] std::array<char, risk::kClientIdBytes>
client_id_for_risk(const orders::ClientOrderId& id) noexcept {
  std::array<char, risk::kClientIdBytes> out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = id.bytes[i];
  }
  return out;
}

} // namespace

EnginePipeline::EnginePipeline(const strategy::IStrategy& strategy, risk::RiskGate& gate,
                               orders::ClientOrderIdGenerator& id_source,
                               EnginePipelineConfig config)
    : strategy_(strategy), gate_(gate), id_source_(id_source), config_(config) {}

EnginePipeline::~EnginePipeline() {
  request_stop();
  join();
}

void EnginePipeline::set_portfolio_view(risk::PortfolioView view) { view_ = view; }

void EnginePipeline::set_journal_drain(JournalDrain drain) { journal_drain_ = drain; }

void EnginePipeline::set_param_blob(const void* data, std::size_t size, std::uint64_t version) {
  param_version_ = version;
  param_size_ = size < kParamStorageBytes ? size : kParamStorageBytes;
  if (data != nullptr && param_size_ > 0) {
    std::memcpy(param_bytes_.data(), data, param_size_);
  }
}

void EnginePipeline::start_and_warmup(const std::uint32_t warmup_cycles) {
  if (running_.load(std::memory_order_acquire)) {
    return;
  }
  stop_.store(false, std::memory_order_release);
  running_.store(true, std::memory_order_release);

  t_market_data_ = std::thread([this] { run_market_data(); });
  t_strategy_ = std::thread([this] { run_strategy(); });
  t_risk_ = std::thread([this] { run_risk_gate(); });
  t_execution_ = std::thread([this] { run_execution(); });
  t_journal_ = std::thread([this] { run_journal_writer(); });

  for (std::uint32_t i = 0; i < warmup_cycles; ++i) {
    marketdata::QuoteIngress warm{};
    warm.warmup = true;
    warm.sequence = i + 1;
    while (!submit_quote(warm) && !stop_.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (sink_.completed.load(std::memory_order_acquire) < warmup_cycles &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
}

bool EnginePipeline::submit_quote(const marketdata::QuoteIngress& quote) noexcept {
  return inject_to_md_.try_push(quote);
}

void EnginePipeline::request_stop() noexcept { stop_.store(true, std::memory_order_release); }

void EnginePipeline::join() {
  if (t_market_data_.joinable()) {
    t_market_data_.join();
  }
  if (t_strategy_.joinable()) {
    t_strategy_.join();
  }
  if (t_risk_.joinable()) {
    t_risk_.join();
  }
  if (t_execution_.joinable()) {
    t_execution_.join();
  }
  if (t_journal_.joinable()) {
    t_journal_.join();
  }
  running_.store(false, std::memory_order_release);
}

risk::PortfolioView EnginePipeline::view_copy() const noexcept { return view_; }

void EnginePipeline::run_market_data() {
  pin_if_requested(config_.cores.market_data, config_.pin_threads);
  static thread_local core::ObjectPool<marketdata::QuoteIngress, 16> staging_pool;
  while (!stop_.load(std::memory_order_acquire)) {
    auto ingress = inject_to_md_.try_pop();
    if (!ingress.has_value()) {
      std::this_thread::yield();
      continue;
    }
    marketdata::QuoteIngress* staged = staging_pool.acquire();
    if (staged == nullptr) {
      continue;
    }
    *staged = *ingress;
    (void)md_to_strategy_.try_push(*staged);
    staging_pool.release(staged);
  }
  while (auto tail = inject_to_md_.try_pop()) {
    marketdata::QuoteIngress* staged = staging_pool.acquire();
    if (staged == nullptr) {
      continue;
    }
    *staged = *tail;
    (void)md_to_strategy_.try_push(*staged);
    staging_pool.release(staged);
  }
}

void EnginePipeline::run_strategy() {
  pin_if_requested(config_.cores.strategy, config_.pin_threads);
  std::array<strategy::OrderIntent, 4> intents{};
  while (!stop_.load(std::memory_order_acquire)) {
    auto ingress = md_to_strategy_.try_pop();
    if (!ingress.has_value()) {
      std::this_thread::yield();
      continue;
    }
    if (ingress->warmup) {
      strategy::IntentIngress out{};
      out.warmup = true;
      (void)strategy_to_risk_.try_push(out);
      continue;
    }
    const strategy::Quote quotes[] = {ingress->quote};
    const strategy::MarketSnapshot market{ingress->sequence, std::span<const strategy::Quote>(quotes)};
    const strategy::ParamView params{param_version_,
                                     std::span<const std::byte>(param_bytes_.data(), param_size_)};
    const std::size_t count = strategy_.decide(market, params, std::span<strategy::OrderIntent>(intents));
    for (std::size_t i = 0; i < count; ++i) {
      strategy::IntentIngress out{};
      out.intent = intents[i];
      (void)strategy_to_risk_.try_push(out);
    }
  }
  while (auto tail = md_to_strategy_.try_pop()) {
    if (tail->warmup) {
      strategy::IntentIngress out{};
      out.warmup = true;
      (void)strategy_to_risk_.try_push(out);
      continue;
    }
    const strategy::Quote quotes[] = {tail->quote};
    const strategy::MarketSnapshot market{tail->sequence, std::span<const strategy::Quote>(quotes)};
    const strategy::ParamView params{param_version_,
                                     std::span<const std::byte>(param_bytes_.data(), param_size_)};
    const std::size_t count = strategy_.decide(market, params, std::span<strategy::OrderIntent>(intents));
    for (std::size_t i = 0; i < count; ++i) {
      strategy::IntentIngress out{};
      out.intent = intents[i];
      (void)strategy_to_risk_.try_push(out);
    }
  }
}

void EnginePipeline::run_risk_gate() {
  pin_if_requested(config_.cores.risk_gate, config_.pin_threads);
  while (!stop_.load(std::memory_order_acquire)) {
    auto ingress = strategy_to_risk_.try_pop();
    if (!ingress.has_value()) {
      std::this_thread::yield();
      continue;
    }
    if (ingress->warmup) {
      risk::ApprovedIngress out{};
      out.warmup = true;
      (void)risk_to_execution_.try_push(out);
      continue;
    }
    const auto id = id_source_.next();
    if (!id.is_ok()) {
      continue;
    }
    risk::GateRequest request{};
    request.intent = ingress->intent;
    request.client_id = client_id_for_risk(id.value());
    request.kind = risk::RequestKind::Order;
    const std::int64_t now = core::Clock::monotonicNs();
    const auto result = gate_.evaluate(request, view_copy(), now);
    if (result.status != risk::GateStatus::Approved) {
      continue;
    }
    risk::ApprovedIngress out{};
    out.intent = ingress->intent;
    out.client_id = request.client_id;
    out.token = result.token;
    (void)risk_to_execution_.try_push(out);

    risk::JournalIngress journal{};
    journal.record.approved = true;
    journal.record.client_id = request.client_id;
    journal.record.limit_set_version = result.token.limit_set_version;
    journal.record.expiry_mono_ns = result.token.expiry_mono_ns;
    (void)risk_to_journal_.try_push(journal);
  }
  while (auto tail = strategy_to_risk_.try_pop()) {
    if (tail->warmup) {
      risk::ApprovedIngress out{};
      out.warmup = true;
      (void)risk_to_execution_.try_push(out);
      continue;
    }
    const auto id = id_source_.next();
    if (!id.is_ok()) {
      continue;
    }
    risk::GateRequest request{};
    request.intent = tail->intent;
    request.client_id = client_id_for_risk(id.value());
    request.kind = risk::RequestKind::Order;
    const std::int64_t now = core::Clock::monotonicNs();
    const auto result = gate_.evaluate(request, view_copy(), now);
    if (result.status != risk::GateStatus::Approved) {
      continue;
    }
    risk::ApprovedIngress out{};
    out.intent = tail->intent;
    out.client_id = request.client_id;
    out.token = result.token;
    (void)risk_to_execution_.try_push(out);
    risk::JournalIngress journal{};
    journal.record.approved = true;
    journal.record.client_id = request.client_id;
    journal.record.limit_set_version = result.token.limit_set_version;
    journal.record.expiry_mono_ns = result.token.expiry_mono_ns;
    (void)risk_to_journal_.try_push(journal);
  }
}

void EnginePipeline::run_execution() {
  pin_if_requested(config_.cores.execution, config_.pin_threads);
  while (!stop_.load(std::memory_order_acquire)) {
    auto ingress = risk_to_execution_.try_pop();
    if (!ingress.has_value()) {
      std::this_thread::yield();
      continue;
    }
    const std::int64_t now = core::Clock::monotonicNs();
    broker::IBroker* const broker =
        broker_.load(std::memory_order_acquire); // Acquire: matches set_broker release.
    if (broker != nullptr) {
      (void)accept_and_execute(*ingress, gate_, *broker, now, sink_, broker_stats_);
    } else {
      (void)orders::accept_for_execution(*ingress, gate_, now, sink_);
    }
  }
  while (auto tail = risk_to_execution_.try_pop()) {
    const std::int64_t now = core::Clock::monotonicNs();
    broker::IBroker* const broker =
        broker_.load(std::memory_order_acquire); // Acquire: matches set_broker release.
    if (broker != nullptr) {
      (void)accept_and_execute(*tail, gate_, *broker, now, sink_, broker_stats_);
    } else {
      (void)orders::accept_for_execution(*tail, gate_, now, sink_);
    }
  }
}

void EnginePipeline::run_journal_writer() {
  pin_if_requested(config_.cores.journal, config_.pin_threads);
  while (!stop_.load(std::memory_order_acquire)) {
    auto ingress = risk_to_journal_.try_pop();
    if (!ingress.has_value()) {
      std::this_thread::yield();
      continue;
    }
    if (journal_drain_.on_record != nullptr) {
      journal_drain_.on_record(journal_drain_.ctx, ingress->record);
    }
    journal_drained_.fetch_add(1, std::memory_order_release);
  }
  while (auto tail = risk_to_journal_.try_pop()) {
    if (journal_drain_.on_record != nullptr) {
      journal_drain_.on_record(journal_drain_.ctx, tail->record);
    }
    journal_drained_.fetch_add(1, std::memory_order_release);
  }
}

} // namespace trading::trader
