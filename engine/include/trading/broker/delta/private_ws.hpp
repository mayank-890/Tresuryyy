#pragma once

#include "trading/broker/transport.hpp"
#include "trading/broker/types.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace trading::broker::delta {

inline constexpr std::int64_t kPrivateWsHeartbeatIntervalNs = 5'000'000'000;
inline constexpr std::int64_t kPrivateWsHeartbeatTimeoutNs = 15'000'000'000;
inline constexpr std::int64_t kPrivateWsReconnectBaseNs = 1'000'000'000;
inline constexpr std::int64_t kPrivateWsReconnectCapNs = 60'000'000'000;

struct PrivateWsConfig {
  std::string url{"wss://socket.delta.exchange"};
  CredentialPort credentials{};
};

class PrivateWebSocket {
public:
  explicit PrivateWebSocket(PrivateWsConfig config);

  void set_transport(WebSocketTransport* transport);

  [[nodiscard]] bool connect();
  void disconnect();
  void tick(std::int64_t now_mono_ns);

  [[nodiscard]] bool connected() const noexcept { return connected_; }
  [[nodiscard]] std::uint32_t reconnect_count() const noexcept { return reconnect_count_; }
  [[nodiscard]] std::vector<PrivateEvent> drain_events();

private:
  void send_subscribe();
  void send_heartbeat();
  void handle_message(std::string_view text);
  void schedule_reconnect(std::int64_t now_mono_ns);
  [[nodiscard]] std::int64_t reconnect_delay_ns(std::uint32_t attempt) const noexcept;

  PrivateWsConfig config_;
  WebSocketTransport* transport_{nullptr};
  bool connected_{false};
  bool awaiting_reconnect_{false};
  std::int64_t next_reconnect_mono_ns_{0};
  std::int64_t last_outbound_mono_ns_{0};
  std::int64_t last_inbound_mono_ns_{0};
  std::uint32_t reconnect_count_{0};
  std::deque<PrivateEvent> events_{};
};

} // namespace trading::broker::delta
