#include "config_error.hpp"
#include "profile_startup.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_running{true};

void on_signal(int) { g_running.store(false); }

struct Options {
  std::string profile;
  std::filesystem::path config_root{"."};
  bool smoke{false};
};

[[nodiscard]] bool parse_args(int argc, char** argv, Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--profile" && i + 1 < argc) {
      options.profile = argv[++i];
      continue;
    }
    if (arg == "--config-root" && i + 1 < argc) {
      options.config_root = argv[++i];
      continue;
    }
    if (arg == "--smoke") {
      options.smoke = true;
      continue;
    }
    std::cerr << "unknown argument: " << arg << '\n';
    return false;
  }
  return !options.profile.empty();
}

void log_config_error(const trading::config::ConfigError& error) {
  std::cerr << "config error: " << trading::config::to_string(error.code) << ": "
            << error.message << '\n';
  if (error.code == trading::config::ConfigErrorCode::PromotionMissing ||
      error.code == trading::config::ConfigErrorCode::PromotionInvalid ||
      error.code == trading::config::ConfigErrorCode::PromotionHashMismatch ||
      error.code == trading::config::ConfigErrorCode::PromotionExpired ||
      error.code == trading::config::ConfigErrorCode::PromotionSignatureInvalid) {
    std::cerr << "event=AL-13-equiv reason=promotion_required\n";
  }
}

} // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse_args(argc, argv, options)) {
    std::cerr << "usage: trader --profile <name> [--config-root path] [--smoke]\n";
    return 2;
  }

  trading::config::StartupContext context;
  const bool groww_enabled =
#ifdef TRADING_ENABLE_GROWW
      true;
#else
      false;
#endif
  const bool live_enabled =
#ifdef TRADING_ENABLE_LIVE_PROFILE
      true;
#else
      false;
#endif

  if (auto error = trading::config::run_profile_startup(options.config_root, options.profile,
                                                        groww_enabled, live_enabled, context)) {
    log_config_error(*error);
    return 1;
  }

  std::cout << "trading-engine " << TRADING_SYSTEM_VERSION << '\n';
  std::cout << "profile=" << context.profile_name << " config_hash=" << context.config_hash_hex
            << '\n';
  std::cout << "journal_config_hash_recorded=" << context.config_hash_hex << '\n';
  std::cout << "RUNNING (no trading logic)\n";

  if (options.smoke) {
    return 0;
  }

  std::signal(SIGTERM, on_signal);
  std::signal(SIGINT, on_signal);
  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
  }
  return 0;
}
