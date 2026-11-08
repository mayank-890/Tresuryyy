#pragma once

#include <cstdint>

namespace trading::broker {

enum class BrokerError : std::uint8_t {
  TransportFailed = 1,
  AuthFailed = 2,
  RateLimited = 3,
  InvalidResponse = 4,
  CredentialsMissing = 5,
  NotConnected = 6,
  JsonError = 7,
};

inline constexpr std::uint16_t kAlertAl12 = 12;

} // namespace trading::broker
