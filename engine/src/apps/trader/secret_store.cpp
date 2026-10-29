#include "secret_store.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace trading::config {

std::string secret_env_var_name(std::string_view secret_name) {
  std::ostringstream oss;
  oss << "TRADING_SECRET_";
  for (char ch : secret_name) {
    if (ch == '/') {
      oss << '_';
    } else if (ch == '-') {
      oss << '_';
    } else {
      oss << static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
  }
  return oss.str();
}

std::optional<ConfigError> ensure_secret_available(std::string_view secret_name) {
  const std::string env_name = secret_env_var_name(secret_name);
  const char* value = std::getenv(env_name.c_str());
  if (value == nullptr || value[0] == '\0') {
    return ConfigError{ConfigErrorCode::SecretMissing,
                       "missing secret (name only): " + std::string(secret_name)};
  }
  return std::nullopt;
}

} // namespace trading::config
