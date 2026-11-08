#pragma once

#include "trading/marketdata/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace trading::marketdata {

// SAMPLE wire format for tests — not a broker protocol (T-0306).
inline constexpr char kGenericFrameMagic[4] = {'G', 'M', 'D', '0'};
inline constexpr std::uint8_t kGenericFrameVersion = 1;
inline constexpr std::uint8_t kGenericKindQuote = 1;
inline constexpr std::uint8_t kGenericKindTrade = 2;

inline constexpr std::size_t kGenericHeaderSize = 28;
inline constexpr std::size_t kGenericQuoteFrameSize = 52;
inline constexpr std::size_t kGenericTradeFrameSize = 44;

enum class ParseStatus : std::uint8_t {
  Ok = 1,
  Malformed = 2,
};

struct ParsedFrame {
  ParseStatus status{ParseStatus::Malformed};
  Quote quote{};
  Trade trade{};
  bool is_quote{false};
};

[[nodiscard]] std::vector<std::byte> encode_quote_frame(const Quote& quote) noexcept;
[[nodiscard]] std::vector<std::byte> encode_trade_frame(const Trade& trade) noexcept;
[[nodiscard]] ParsedFrame parse_generic_frame(std::span<const std::byte> frame) noexcept;

} // namespace trading::marketdata
