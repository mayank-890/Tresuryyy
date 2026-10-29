#include "toml_json.hpp"

#include <stdexcept>

namespace trading::config {

namespace {

nlohmann::json table_to_json(const toml::table& table) {
  nlohmann::json out = nlohmann::json::object();
  for (const auto& [key, value] : table) {
    out[key] = toml_to_json(value);
  }
  return out;
}

} // namespace

nlohmann::json toml_to_json(const toml::node& node) {
  if (node.is_table()) {
    return table_to_json(*node.as_table());
  }
  if (node.is_array()) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& element : *node.as_array()) {
      arr.push_back(toml_to_json(element));
    }
    return arr;
  }
  if (node.is_string()) {
    return node.value_or(std::string{});
  }
  if (node.is_integer()) {
    return node.value_or(int64_t{0});
  }
  if (node.is_floating_point()) {
    return node.value_or(0.0);
  }
  if (node.is_boolean()) {
    return node.value_or(false);
  }
  throw std::runtime_error("unsupported toml node type");
}

} // namespace trading::config
