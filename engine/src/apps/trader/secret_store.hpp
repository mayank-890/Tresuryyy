#pragma once

#include "config_error.hpp"

#include <optional>
#include <string>

namespace trading::config {

[[nodiscard]] std::string secret_env_var_name(std::string_view secret_name);

[[nodiscard]] std::optional<ConfigError> ensure_secret_available(std::string_view secret_name);

} // namespace trading::config
