#pragma once

#include <string>

namespace trading::config {

enum class ConfigErrorCode {
  ProfileMissingName,
  UnknownKey,
  SchemaViolation,
  ProfileReadFailed,
  PromotionMissing,
  PromotionInvalid,
  PromotionHashMismatch,
  PromotionExpired,
  PromotionSignatureInvalid,
  LiveProfileDisabled,
  GrowwAdapterDisabled,
  SecretMissing,
  InvalidArgument,
};

struct ConfigError {
  ConfigErrorCode code{};
  std::string message; // Must not contain secret values.
};

[[nodiscard]] inline const char* to_string(ConfigErrorCode code) {
  switch (code) {
  case ConfigErrorCode::ProfileMissingName:
    return "ProfileMissingName";
  case ConfigErrorCode::UnknownKey:
    return "UnknownKey";
  case ConfigErrorCode::SchemaViolation:
    return "SchemaViolation";
  case ConfigErrorCode::ProfileReadFailed:
    return "ProfileReadFailed";
  case ConfigErrorCode::PromotionMissing:
    return "PromotionMissing";
  case ConfigErrorCode::PromotionInvalid:
    return "PromotionInvalid";
  case ConfigErrorCode::PromotionHashMismatch:
    return "HashMismatch";
  case ConfigErrorCode::PromotionExpired:
    return "PromotionExpired";
  case ConfigErrorCode::PromotionSignatureInvalid:
    return "PromotionSignatureInvalid";
  case ConfigErrorCode::LiveProfileDisabled:
    return "LiveProfileDisabled";
  case ConfigErrorCode::GrowwAdapterDisabled:
    return "GrowwAdapterDisabled";
  case ConfigErrorCode::SecretMissing:
    return "SecretMissing";
  case ConfigErrorCode::InvalidArgument:
    return "InvalidArgument";
  }
  return "Unknown";
}

} // namespace trading::config
