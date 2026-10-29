#pragma once

#include <nlohmann/json.hpp>
#include <toml++/toml.h>

namespace trading::config {

[[nodiscard]] nlohmann::json toml_to_json(const toml::node& node);

} // namespace trading::config
