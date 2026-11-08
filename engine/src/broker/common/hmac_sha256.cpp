#include "trading/broker/hmac_sha256.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

#include <openssl/hmac.h>

namespace trading::broker {

std::string hmac_sha256_hex(const std::string_view secret, const std::string_view message) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_len = 0;
  HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
       reinterpret_cast<const unsigned char*>(message.data()), message.size(), digest, &digest_len);

  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < digest_len; ++i) {
    out << std::setw(2) << static_cast<unsigned>(digest[i]);
  }
  return out.str();
}

} // namespace trading::broker
