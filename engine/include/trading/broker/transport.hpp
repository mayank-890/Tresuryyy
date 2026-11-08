#pragma once

#include "trading/broker/errors.hpp"
#include "trading/core/result.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace trading::broker {

struct HttpRequest {
  std::string method;
  std::string path;
  std::string query;
  std::string body;
  std::map<std::string, std::string> headers;
};

struct HttpResponse {
  int status{0};
  std::string body;
};

struct HttpTransport {
  virtual ~HttpTransport() = default;
  [[nodiscard]] virtual core::Result<HttpResponse, BrokerError> send(const HttpRequest& request) = 0;
};

struct WebSocketTransport {
  virtual ~WebSocketTransport() = default;
  [[nodiscard]] virtual bool connect(std::string_view url) = 0;
  virtual void disconnect() = 0;
  [[nodiscard]] virtual bool send(std::string_view text) = 0;
  [[nodiscard]] virtual std::optional<std::string> try_recv() = 0;
  [[nodiscard]] virtual bool connected() const = 0;
};

} // namespace trading::broker
