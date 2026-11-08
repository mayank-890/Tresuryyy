#include "trading/broker/delta/signing.hpp"

#include "trading/broker/hmac_sha256.hpp"

namespace trading::broker::delta {

std::string build_signature_payload(const std::string_view method, const std::string_view timestamp,
                                     const std::string_view path, const std::string_view query,
                                     const std::string_view body) {
  std::string payload;
  payload.reserve(method.size() + timestamp.size() + path.size() + query.size() + body.size());
  payload.append(method);
  payload.append(timestamp);
  payload.append(path);
  payload.append(query);
  payload.append(body);
  return payload;
}

std::string sign_request(const std::string_view api_secret, const std::string_view method,
                          const std::string_view timestamp, const std::string_view path,
                          const std::string_view query, const std::string_view body) {
  const std::string payload = build_signature_payload(method, timestamp, path, query, body);
  return hmac_sha256_hex(api_secret, payload);
}

} // namespace trading::broker::delta
