#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace trading::config {

void sort_json_keys(nlohmann::json& value);

[[nodiscard]] std::string profile_content_hash_hex(const nlohmann::json& profile_document);

} // namespace trading::config
