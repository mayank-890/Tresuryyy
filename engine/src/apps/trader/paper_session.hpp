#pragma once

#include "config_error.hpp"
#include "profile_startup.hpp"
#include "trading/broker/mock/adapter.hpp"
#include "trading/portfolio/reconciler.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace trading::trader {

struct PaperStartupResult {
  bool ok{false};
  std::string detail;
};

// FR-OP-01 / FR-BR-07 — mock broker connect + startup reconciliation (no network).
class PaperTradingSession {
public:
  PaperTradingSession() = default;
  ~PaperTradingSession();
  PaperTradingSession(const PaperTradingSession&) = delete;
  PaperTradingSession& operator=(const PaperTradingSession&) = delete;
  [[nodiscard]] std::optional<trading::config::ConfigError>
  load_profile(const std::filesystem::path& config_root, std::string_view profile_name,
               bool groww_enabled, bool live_profile_enabled);

  void adopt_startup(trading::config::StartupContext context) { startup_ = std::move(context); }

  [[nodiscard]] PaperStartupResult bootstrap_mock_broker(
      const std::filesystem::path& mock_scenario_path, std::int64_t now_mono_ns);

  [[nodiscard]] broker::IBroker& broker() noexcept {
    return *reinterpret_cast<broker::mock::MockExchangeBroker*>(broker_storage_);
  }
  [[nodiscard]] const broker::mock::MockExchangeBroker& mock_broker() const noexcept {
    return *reinterpret_cast<const broker::mock::MockExchangeBroker*>(broker_storage_);
  }
  [[nodiscard]] const portfolio::Reconciler& reconciler() const noexcept { return reconciler_; }
  [[nodiscard]] const trading::config::StartupContext& startup() const noexcept { return startup_; }

  [[nodiscard]] bool uses_mock_adapter() const noexcept {
    return startup_.execution_adapter == "mock";
  }

private:
  trading::config::StartupContext startup_{};
  alignas(broker::mock::MockExchangeBroker) unsigned char
      broker_storage_[sizeof(broker::mock::MockExchangeBroker)]{};
  portfolio::Reconciler reconciler_{};
  bool broker_constructed_{false};
};

} // namespace trading::trader
