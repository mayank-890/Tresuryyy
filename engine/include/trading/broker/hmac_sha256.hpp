#pragma once

#include <string>
#include <string_view>

namespace trading::broker {

[[nodiscard]] std::string hmac_sha256_hex(std::string_view secret, std::string_view message);

} // namespace trading::broker
