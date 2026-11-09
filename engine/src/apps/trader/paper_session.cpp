#include "paper_session.hpp"

#include "paper_paths.hpp"

namespace trading::trader {

namespace {

void load_empty_snapshot(void* /*ctx*/, portfolio::PortfolioSnapshot& out) { out = {}; }

bool fetch_broker_snapshot(void* ctx, portfolio::PortfolioSnapshot& out) {
  auto* session = static_cast<PaperTradingSession*>(ctx);
  out = {};
  if (!session->mock_broker().health().rest_ok) {
    return false;
  }
  return true;
}

} // namespace

PaperTradingSession::~PaperTradingSession() {
  if (broker_constructed_) {
    mock_broker().~MockExchangeBroker();
    broker_constructed_ = false;
  }
}

std::optional<trading::config::ConfigError>
PaperTradingSession::load_profile(const std::filesystem::path& config_root,
                                   const std::string_view profile_name, const bool groww_enabled,
                                   const bool live_profile_enabled) {
  return trading::config::run_profile_startup(config_root, profile_name, groww_enabled,
                                              live_profile_enabled, startup_);
}

PaperStartupResult PaperTradingSession::bootstrap_mock_broker(
    const std::filesystem::path& mock_scenario_path, const std::int64_t now_mono_ns) {
  PaperStartupResult result{};
  if (startup_.profile_mode != "paper") {
    result.detail = "profile mode is not paper";
    return result;
  }
  if (startup_.execution_adapter != "mock") {
    result.detail = "paper profile must use mock adapter";
    return result;
  }

  const auto scenario = broker::mock::load_scenario_from_file(mock_scenario_path.string());
  if (!scenario.is_ok()) {
    result.detail = "mock scenario load failed";
    return result;
  }

  if (broker_constructed_) {
    mock_broker().~MockExchangeBroker();
  }
  new (broker_storage_) broker::mock::MockExchangeBroker(std::move(scenario.value()));
  broker_constructed_ = true;
  if (!broker().connect()) {
    result.detail = "mock broker connect failed";
    return result;
  }

  portfolio::SnapshotPorts snapshots{};
  snapshots.ctx = this;
  snapshots.load_internal = load_empty_snapshot;
  snapshots.fetch_broker = fetch_broker_snapshot;

  portfolio::JournalPort journal{};
  portfolio::AlertPort alerts{};
  reconciler_.set_ports(snapshots, journal, alerts);

  const auto reconcile = reconciler_.on_connect(now_mono_ns);
  if (reconcile.outcome != journal::v1::ReconciliationOutcome::Clean) {
    result.detail = "startup reconciliation mismatch";
    return result;
  }
  if (!reconciler_.startup_reconcile_clean() || !reconciler_.trading_allowed()) {
    result.detail = "trading not allowed after startup reconcile";
    return result;
  }

  result.ok = true;
  return result;
}

} // namespace trading::trader
