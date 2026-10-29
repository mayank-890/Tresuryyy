#pragma once

#include "config_error.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace trading::config {

struct StartupContext {
  std::string profile_name;
  std::string config_hash_hex;
};

[[nodiscard]] std::optional<ConfigError> run_profile_startup(
    const std::filesystem::path& config_root, std::string_view profile_name,
    bool groww_enabled, bool live_profile_enabled, StartupContext& out);

} // namespace trading::config
