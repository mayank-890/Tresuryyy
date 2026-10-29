#include "profile_startup.hpp"

#include "content_hash.hpp"
#include "promotion_verifier.hpp"
#include "schema_validator.hpp"
#include "secret_store.hpp"
#include "toml_json.hpp"

#include <fstream>
#include <toml++/toml.h>

namespace trading::config {

namespace {

std::optional<ConfigError> fail(ConfigErrorCode code, std::string message) {
  return ConfigError{code, std::move(message)};
}

std::optional<ConfigError> load_json_file(const std::filesystem::path& path,
                                          nlohmann::json& out) {
  std::ifstream in(path);
  if (!in) {
    return fail(ConfigErrorCode::ProfileReadFailed, "cannot read " + path.string());
  }
  try {
    in >> out;
  } catch (const nlohmann::json::exception&) {
    return fail(ConfigErrorCode::ProfileReadFailed, "invalid JSON in " + path.string());
  }
  return std::nullopt;
}

} // namespace

std::optional<ConfigError> run_profile_startup(const std::filesystem::path& config_root,
                                               std::string_view profile_name,
                                               bool groww_enabled, bool live_profile_enabled,
                                               StartupContext& out) {
  const auto profile_path = config_root / "profiles" / (std::string(profile_name) + ".toml");
  const auto schema_path = config_root / "schema" / "profile.schema.json";

  nlohmann::json schema;
  if (auto err = load_json_file(schema_path, schema)) {
    return err;
  }

  toml::table table;
  try {
    table = toml::parse_file(profile_path.string());
  } catch (const toml::parse_error& ex) {
    return fail(ConfigErrorCode::ProfileReadFailed, ex.what());
  }

  nlohmann::json document;
  try {
    document = toml_to_json(table);
  } catch (const std::exception& ex) {
    return fail(ConfigErrorCode::ProfileReadFailed, ex.what());
  }

  if (auto err = validate_profile_document(document, schema)) {
    return err;
  }

  const std::string mode = document["profile"]["mode"].get<std::string>();
  if (mode == "live") {
    if (!live_profile_enabled) {
      return fail(ConfigErrorCode::LiveProfileDisabled, "live profile disabled at build time");
    }
    if (!document.contains("promotion")) {
      return fail(ConfigErrorCode::PromotionMissing, "live profile requires promotion block");
    }
  }

  const std::string adapter = document["execution"]["adapter"].get<std::string>();
  if (adapter == "groww" && !groww_enabled) {
    return fail(ConfigErrorCode::GrowwAdapterDisabled,
               "groww adapter referenced but TRADING_ENABLE_GROWW is OFF");
  }

  const std::string url_secret = document["database"]["url_secret"].get<std::string>();
  if (auto err = ensure_secret_available(url_secret)) {
    return err;
  }

  out.config_hash_hex = profile_content_hash_hex(document);
  out.profile_name = document["profile"]["name"].get<std::string>();

  if (mode == "live") {
    const auto promotion_rel = document["promotion"]["file"].get<std::string>();
    const auto public_key_pem = document["promotion"]["public_key_pem"].get<std::string>();
    std::filesystem::path promotion_path = promotion_rel;
    if (promotion_path.is_relative()) {
      promotion_path = config_root / promotion_path;
    }
    if (auto err =
            verify_promotion_file(config_root, promotion_path, public_key_pem, out.config_hash_hex)) {
      return err;
    }
  }

  return std::nullopt;
}

} // namespace trading::config
