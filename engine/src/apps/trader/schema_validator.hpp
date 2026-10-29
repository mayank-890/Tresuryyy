#pragma once

#include "config_error.hpp"

#include <nlohmann/json.hpp>
#include <optional>

namespace trading::config {

[[nodiscard]] std::optional<ConfigError> validate_profile_document(
    const nlohmann::json& document, const nlohmann::json& schema);

} // namespace trading::config
