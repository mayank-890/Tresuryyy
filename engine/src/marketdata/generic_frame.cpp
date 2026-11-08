#include "trading/marketdata/generic_frame.hpp"

#include <cstring>
#include <type_traits>

namespace trading::marketdata {

namespace {

void append_u8(std::vector<std::byte>& out, std::uint8_t value) {
  out.push_back(static_cast<std::byte>(value));
}

void append_u16(std::vector<std::byte>& out, std::uint16_t value) {
  for (int i = 0; i < 2; ++i) {
    out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
  }
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
  }
}

void append_u64(std::vector<std::byte>& out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
  }
}

void append_i32(std::vector<std::byte>& out, std::int32_t value) {
  append_u32(out, static_cast<std::uint32_t>(value));
}

void append_i64(std::vector<std::byte>& out, std::int64_t value) {
  append_u64(out, static_cast<std::uint64_t>(value));
}

[[nodiscard]] bool read_u8(std::span<const std::byte> data, std::size_t& offset, std::uint8_t& out) {
  if (offset + 1 > data.size()) {
    return false;
  }
  out = static_cast<std::uint8_t>(data[offset]);
  offset += 1;
  return true;
}

[[nodiscard]] bool read_u16(std::span<const std::byte> data, std::size_t& offset, std::uint16_t& out) {
  if (offset + 2 > data.size()) {
    return false;
  }
  out = 0;
  for (int i = 0; i < 2; ++i) {
    out = static_cast<std::uint16_t>(out | (static_cast<std::uint16_t>(data[offset + i]) << (8 * i)));
  }
  offset += 2;
  return true;
}

[[nodiscard]] bool read_u32(std::span<const std::byte> data, std::size_t& offset, std::uint32_t& out) {
  if (offset + 4 > data.size()) {
    return false;
  }
  out = 0;
  for (int i = 0; i < 4; ++i) {
    out |= static_cast<std::uint32_t>(data[offset + i]) << (8 * i);
  }
  offset += 4;
  return true;
}

[[nodiscard]] bool read_u64(std::span<const std::byte> data, std::size_t& offset, std::uint64_t& out) {
  if (offset + 8 > data.size()) {
    return false;
  }
  out = 0;
  for (int i = 0; i < 8; ++i) {
    out |= static_cast<std::uint64_t>(data[offset + i]) << (8 * i);
  }
  offset += 8;
  return true;
}

[[nodiscard]] bool read_i32(std::span<const std::byte> data, std::size_t& offset, std::int32_t& out) {
  std::uint32_t raw = 0;
  if (!read_u32(data, offset, raw)) {
    return false;
  }
  out = static_cast<std::int32_t>(raw);
  return true;
}

[[nodiscard]] bool read_i64(std::span<const std::byte> data, std::size_t& offset, std::int64_t& out) {
  std::uint64_t raw = 0;
  if (!read_u64(data, offset, raw)) {
    return false;
  }
  out = static_cast<std::int64_t>(raw);
  return true;
}

void append_header(std::vector<std::byte>& out, std::uint8_t kind, std::uint32_t instrument_id,
                   std::uint64_t sequence, std::uint64_t exchange_time_ns) {
  for (char ch : kGenericFrameMagic) {
    out.push_back(static_cast<std::byte>(ch));
  }
  append_u8(out, kGenericFrameVersion);
  append_u8(out, kind);
  append_u16(out, 0);
  append_u32(out, instrument_id);
  append_u64(out, sequence);
  append_u64(out, exchange_time_ns);
}

[[nodiscard]] bool magic_ok(std::span<const std::byte> frame) {
  if (frame.size() < 4) {
    return false;
  }
  for (std::size_t i = 0; i < 4; ++i) {
    if (frame[i] != static_cast<std::byte>(kGenericFrameMagic[i])) {
      return false;
    }
  }
  return true;
}

} // namespace

std::vector<std::byte> encode_quote_frame(const Quote& quote) noexcept {
  std::vector<std::byte> out;
  out.reserve(kGenericQuoteFrameSize);
  append_header(out, kGenericKindQuote, quote.instrument_id, quote.sequence, quote.exchange_time_ns);
  append_i64(out, quote.bid_ticks);
  append_i64(out, quote.ask_ticks);
  append_i32(out, quote.bid_size_lots);
  append_i32(out, quote.ask_size_lots);
  return out;
}

std::vector<std::byte> encode_trade_frame(const Trade& trade) noexcept {
  std::vector<std::byte> out;
  out.reserve(kGenericTradeFrameSize);
  append_header(out, kGenericKindTrade, trade.instrument_id, trade.sequence, trade.exchange_time_ns);
  append_i64(out, trade.price_ticks);
  append_i32(out, trade.size_lots);
  append_u16(out, static_cast<std::uint16_t>(
                     static_cast<std::underlying_type_t<journal::v1::Side>>(trade.side)));
  append_u16(out, 0);
  return out;
}

ParsedFrame parse_generic_frame(const std::span<const std::byte> frame) noexcept {
  ParsedFrame parsed{};
  parsed.status = ParseStatus::Malformed;
  if (!magic_ok(frame)) {
    return parsed;
  }

  std::size_t offset = 0;
  std::uint8_t version = 0;
  std::uint8_t kind = 0;
  std::uint16_t reserved = 0;
  std::uint32_t instrument_id = 0;
  std::uint64_t sequence = 0;
  std::uint64_t exchange_time_ns = 0;

  offset += 4;
  if (!read_u8(frame, offset, version) || version != kGenericFrameVersion) {
    return parsed;
  }
  if (!read_u8(frame, offset, kind)) {
    return parsed;
  }
  if (!read_u16(frame, offset, reserved)) {
    return parsed;
  }
  if (!read_u32(frame, offset, instrument_id) || !read_u64(frame, offset, sequence) ||
      !read_u64(frame, offset, exchange_time_ns)) {
    return parsed;
  }

  if (kind == kGenericKindQuote) {
    if (frame.size() != kGenericQuoteFrameSize) {
      return parsed;
    }
    Quote quote{};
    quote.instrument_id = instrument_id;
    quote.sequence = sequence;
    quote.exchange_time_ns = exchange_time_ns;
    if (!read_i64(frame, offset, quote.bid_ticks) || !read_i64(frame, offset, quote.ask_ticks) ||
        !read_i32(frame, offset, quote.bid_size_lots) ||
        !read_i32(frame, offset, quote.ask_size_lots) || offset != frame.size()) {
      return parsed;
    }
    parsed.is_quote = true;
    parsed.quote = quote;
    parsed.status = ParseStatus::Ok;
    return parsed;
  }

  if (kind == kGenericKindTrade) {
    if (frame.size() != kGenericTradeFrameSize) {
      return parsed;
    }
    Trade trade{};
    trade.instrument_id = instrument_id;
    trade.sequence = sequence;
    trade.exchange_time_ns = exchange_time_ns;
    std::uint16_t side_raw = 0;
    std::uint16_t pad = 0;
    if (!read_i64(frame, offset, trade.price_ticks) || !read_i32(frame, offset, trade.size_lots) ||
        !read_u16(frame, offset, side_raw) || !read_u16(frame, offset, pad) ||
        offset != frame.size()) {
      return parsed;
    }
    trade.side = static_cast<journal::v1::Side>(side_raw);
    parsed.is_quote = false;
    parsed.trade = trade;
    parsed.status = ParseStatus::Ok;
    return parsed;
  }

  return parsed;
}

} // namespace trading::marketdata
