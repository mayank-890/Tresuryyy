#include "promotion_verifier.hpp"

#include "content_hash.hpp"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace trading::config {

namespace {

std::optional<ConfigError> fail(ConfigErrorCode code, std::string message) {
  return ConfigError{code, std::move(message)};
}

std::optional<std::chrono::system_clock::time_point> parse_iso8601_utc(std::string_view text) {
  std::tm tm{};
  std::istringstream iss{std::string(text)};
  iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
  if (iss.fail()) {
    return std::nullopt;
  }
#if defined(_WIN32)
  const time_t seconds = _mkgmtime(&tm);
#elif defined(__APPLE__)
  tm.tm_isdst = 0;
  setenv("TZ", "UTC", 1);
  tzset();
  const time_t seconds = mktime(&tm);
#else
  const time_t seconds = timegm(&tm);
#endif
  if (seconds == static_cast<time_t>(-1)) {
    return std::nullopt;
  }
  return std::chrono::system_clock::from_time_t(seconds);
}

std::optional<std::vector<unsigned char>> base64_decode(std::string_view input) {
  BIO* b64 = BIO_new(BIO_f_base64());
  BIO* bio = BIO_new_mem_buf(input.data(), static_cast<int>(input.size()));
  bio = BIO_push(b64, bio);
  BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
  std::vector<unsigned char> buffer(input.size());
  const int decoded = BIO_read(bio, buffer.data(), static_cast<int>(buffer.size()));
  BIO_free_all(bio);
  if (decoded <= 0) {
    return std::nullopt;
  }
  buffer.resize(static_cast<std::size_t>(decoded));
  return buffer;
}

} // namespace

std::string promotion_signing_payload(const nlohmann::json& document) {
  nlohmann::json payload = document;
  payload.erase("signature");
  sort_json_keys(payload);
  return payload.dump();
}

std::optional<ConfigError> validate_promotion_document(const nlohmann::json& document,
                                                       const nlohmann::json& schema) {
  if (!document.is_object()) {
    return fail(ConfigErrorCode::PromotionInvalid, "promotion file must be a JSON object");
  }
  if (schema.contains("additionalProperties") && schema["additionalProperties"] == false) {
    for (auto it = document.begin(); it != document.end(); ++it) {
      if (!schema["properties"].contains(it.key())) {
        return fail(ConfigErrorCode::PromotionInvalid, "unknown promotion field");
      }
    }
  }
  for (const auto& required : schema["required"]) {
    if (!document.contains(required.get<std::string>())) {
      return fail(ConfigErrorCode::PromotionInvalid, "promotion missing required field");
    }
  }
  if (document["format_version"].get<int>() != 1) {
    return fail(ConfigErrorCode::PromotionInvalid, "unsupported promotion format_version");
  }
  const std::regex hex64("^[0-9a-f]{64}$");
  for (const char* field : {"engine_build_hash", "config_hash", "paper_report_hash"}) {
    const auto value = document[field].get<std::string>();
    if (!std::regex_match(value, hex64)) {
      return fail(ConfigErrorCode::PromotionInvalid, "promotion hash field invalid");
    }
  }
  return std::nullopt;
}

std::optional<ConfigError> verify_promotion_file(const std::filesystem::path& config_root,
                                                 const std::filesystem::path& promotion_path,
                                                 std::string_view public_key_pem,
                                                 std::string_view expected_config_hash_hex) {
  if (!std::filesystem::exists(promotion_path)) {
    return fail(ConfigErrorCode::PromotionMissing,
               "promotion file missing: " + promotion_path.string());
  }

  std::ifstream in(promotion_path);
  if (!in) {
    return fail(ConfigErrorCode::PromotionInvalid, "promotion file unreadable");
  }
  nlohmann::json document;
  try {
    in >> document;
  } catch (const nlohmann::json::exception&) {
    return fail(ConfigErrorCode::PromotionInvalid, "promotion file is not valid JSON");
  }

  const auto schema_path = config_root / "schema" / "promotion.schema.json";
  nlohmann::json schema;
  if (std::filesystem::exists(schema_path)) {
    std::ifstream schema_in(schema_path);
    schema_in >> schema;
  }
  if (!schema.is_null()) {
    if (auto err = validate_promotion_document(document, schema)) {
      return err;
    }
  }

  const std::string config_hash = document["config_hash"].get<std::string>();
  if (config_hash != expected_config_hash_hex) {
    return fail(ConfigErrorCode::PromotionHashMismatch, "promotion config_hash mismatch");
  }

  const auto expires = parse_iso8601_utc(document["expires_at"].get<std::string>());
  if (!expires.has_value()) {
    return fail(ConfigErrorCode::PromotionInvalid, "promotion expires_at invalid");
  }
  if (std::chrono::system_clock::now() > *expires) {
    return fail(ConfigErrorCode::PromotionExpired, "promotion file expired");
  }

  const auto signature = base64_decode(document["signature"].get<std::string>());
  if (!signature.has_value()) {
    return fail(ConfigErrorCode::PromotionSignatureInvalid, "promotion signature decode failed");
  }

  BIO* bio = BIO_new_mem_buf(public_key_pem.data(), static_cast<int>(public_key_pem.size()));
  EVP_PKEY* public_key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (public_key == nullptr) {
    return fail(ConfigErrorCode::PromotionInvalid, "promotion public key invalid");
  }

  const std::string payload = promotion_signing_payload(document);
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (ctx == nullptr) {
    EVP_PKEY_free(public_key);
    return fail(ConfigErrorCode::PromotionInvalid, "EVP_MD_CTX_new failed");
  }

  std::optional<ConfigError> result;
  if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, public_key) != 1) {
    result = fail(ConfigErrorCode::PromotionSignatureInvalid, "DigestVerifyInit failed");
  } else if (EVP_DigestVerify(ctx, signature->data(), signature->size(),
                               reinterpret_cast<const unsigned char*>(payload.data()),
                               payload.size()) != 1) {
    result = fail(ConfigErrorCode::PromotionSignatureInvalid, "promotion signature invalid");
  }

  EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(public_key);
  return result;
}

} // namespace trading::config
