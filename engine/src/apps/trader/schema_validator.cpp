#include "schema_validator.hpp"

#include <regex>

namespace trading::config {

namespace {

std::optional<ConfigError> fail(ConfigErrorCode code, std::string message) {
  return ConfigError{code, std::move(message)};
}

std::optional<ConfigError> validate_node(const nlohmann::json& value,
                                         const nlohmann::json& schema,
                                         const std::string& path);

std::optional<ConfigError> check_type(const nlohmann::json& value,
                                      const nlohmann::json& schema,
                                      const std::string& path) {
  if (!schema.contains("type")) {
    return std::nullopt;
  }
  const auto type = schema["type"].get<std::string>();
  if (type == "object" && !value.is_object()) {
    return fail(ConfigErrorCode::SchemaViolation, path + ": expected object");
  }
  if (type == "string" && !value.is_string()) {
    return fail(ConfigErrorCode::SchemaViolation, path + ": expected string");
  }
  if (type == "integer" && !value.is_number_integer()) {
    return fail(ConfigErrorCode::SchemaViolation, path + ": expected integer");
  }
  return std::nullopt;
}

std::optional<ConfigError> validate_object(const nlohmann::json& value,
                                           const nlohmann::json& schema,
                                           const std::string& path) {
  if (auto err = check_type(value, schema, path)) {
    return err;
  }
  if (!value.is_object()) {
    return fail(ConfigErrorCode::SchemaViolation, path + ": expected object");
  }

  const bool additional_allowed =
      schema.contains("additionalProperties") && schema["additionalProperties"].is_boolean() &&
      schema["additionalProperties"].get<bool>();

  if (schema.contains("properties") && schema["properties"].is_object()) {
    for (auto it = value.begin(); it != value.end(); ++it) {
      if (!schema["properties"].contains(it.key())) {
        if (!additional_allowed) {
          return fail(ConfigErrorCode::UnknownKey, path + ": unknown key '" + it.key() + "'");
        }
      }
    }
  }

  if (schema.contains("required") && schema["required"].is_array()) {
    for (const auto& required_key : schema["required"]) {
      const std::string key = required_key.get<std::string>();
      if (!value.contains(key)) {
        if (path == "$" && key == "name") {
          return fail(ConfigErrorCode::ProfileMissingName, "profile.name is required");
        }
        return fail(ConfigErrorCode::SchemaViolation, path + ": missing required key '" + key + "'");
      }
    }
  }

  if (schema.contains("properties") && schema["properties"].is_object()) {
    for (auto it = schema["properties"].begin(); it != schema["properties"].end(); ++it) {
      if (!value.contains(it.key())) {
        continue;
      }
      const std::string child_path = path + "." + it.key();
      if (auto err = validate_node(value.at(it.key()), it.value(), child_path)) {
        return err;
      }
    }
  }

  if (schema.contains("allOf") && schema["allOf"].is_array()) {
    for (const auto& clause : schema["allOf"]) {
      if (!clause.contains("if") || !clause.contains("then")) {
        continue;
      }
      const auto& if_schema = clause["if"];
      bool matches = true;
      if (if_schema.contains("properties")) {
        for (auto it = if_schema["properties"].begin(); it != if_schema["properties"].end(); ++it) {
          if (!value.contains(it.key())) {
            matches = false;
            break;
          }
          if (auto err = validate_node(value.at(it.key()), it.value(), path + "." + it.key())) {
            matches = false;
            break;
          }
        }
      }
      if (matches && clause.contains("then")) {
        if (auto err = validate_node(value, clause["then"], path)) {
          return err;
        }
      }
    }
  }

  return std::nullopt;
}

std::optional<ConfigError> validate_string(const nlohmann::json& value,
                                           const nlohmann::json& schema,
                                           const std::string& path) {
  if (auto err = check_type(value, schema, path)) {
    return err;
  }
  const std::string s = value.get<std::string>();
  if (schema.contains("minLength") && s.size() < schema["minLength"].get<std::size_t>()) {
    return fail(ConfigErrorCode::SchemaViolation, path + ": string too short");
  }
  if (schema.contains("enum")) {
    bool ok = false;
    for (const auto& option : schema["enum"]) {
      if (option.get<std::string>() == s) {
        ok = true;
        break;
      }
    }
    if (!ok) {
      return fail(ConfigErrorCode::SchemaViolation, path + ": value not in enum");
    }
  }
  if (schema.contains("pattern")) {
    const std::regex pattern(schema["pattern"].get<std::string>());
    if (!std::regex_match(s, pattern)) {
      return fail(ConfigErrorCode::SchemaViolation, path + ": pattern mismatch");
    }
  }
  if (schema.contains("const") && schema["const"].get<std::string>() != s) {
    return fail(ConfigErrorCode::SchemaViolation, path + ": const mismatch");
  }
  return std::nullopt;
}

std::optional<ConfigError> validate_integer(const nlohmann::json& value,
                                            const nlohmann::json& schema,
                                            const std::string& path) {
  if (auto err = check_type(value, schema, path)) {
    return err;
  }
  const auto n = value.get<int64_t>();
  if (schema.contains("minimum") && n < schema["minimum"].get<int64_t>()) {
    return fail(ConfigErrorCode::SchemaViolation, path + ": below minimum");
  }
  if (schema.contains("const") && schema["const"].get<int64_t>() != n) {
    return fail(ConfigErrorCode::SchemaViolation, path + ": const mismatch");
  }
  return std::nullopt;
}

std::optional<ConfigError> validate_node(const nlohmann::json& value,
                                         const nlohmann::json& schema,
                                         const std::string& path) {
  if (schema.contains("type")) {
    const auto type = schema["type"].get<std::string>();
    if (type == "object") {
      return validate_object(value, schema, path);
    }
    if (type == "string") {
      return validate_string(value, schema, path);
    }
    if (type == "integer") {
      return validate_integer(value, schema, path);
    }
  }
  return validate_object(value, schema, path);
}

} // namespace

std::optional<ConfigError> validate_profile_document(const nlohmann::json& document,
                                                     const nlohmann::json& schema) {
  if (document.contains("profile") && document["profile"].is_object()) {
    if (!document["profile"].contains("name") ||
        !document["profile"]["name"].is_string() ||
        document["profile"]["name"].get<std::string>().empty()) {
      return fail(ConfigErrorCode::ProfileMissingName, "profile.name is required");
    }
  } else {
    return fail(ConfigErrorCode::ProfileMissingName, "profile.name is required");
  }
  return validate_node(document, schema, "$");
}

} // namespace trading::config
