#pragma once

#include "config_error.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace trading::config {

[[nodiscard]] std::optional<ConfigError> verify_promotion_file(
    const std::filesystem::path& config_root, const std::filesystem::path& promotion_path,
    std::string_view public_key_pem, std::string_view expected_config_hash_hex);

[[nodiscard]] std::optional<ConfigError> validate_promotion_document(
    const nlohmann::json& document, const nlohmann::json& schema);

[[nodiscard]] std::string promotion_signing_payload(const nlohmann::json& document);

} // namespace trading::config
