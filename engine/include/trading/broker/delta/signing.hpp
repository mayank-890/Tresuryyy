#pragma once

#include <string>
#include <string_view>

namespace trading::broker::delta {

// Delta docs: method + timestamp + path + query_string + body (TI §12.1).
[[nodiscard]] std::string build_signature_payload(std::string_view method, std::string_view timestamp,
                                                   std::string_view path, std::string_view query,
                                                   std::string_view body);

[[nodiscard]] std::string sign_request(std::string_view api_secret, std::string_view method,
                                        std::string_view timestamp, std::string_view path,
                                        std::string_view query, std::string_view body);

} // namespace trading::broker::delta
