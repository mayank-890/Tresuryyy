#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "trading/journal/generated/records.hpp"
#include "trading/journal/writer.hpp"

namespace {

std::vector<std::byte> to_bytes(const std::vector<std::uint8_t>& raw) {
  std::vector<std::byte> out;
  out.reserve(raw.size());
  for (const auto b : raw) {
    out.push_back(static_cast<std::byte>(b));
  }
  return out;
}

} // namespace

TEST(JournalWriter, AppendMarketQuoteRoundTrip) {
  const auto temp_dir = std::filesystem::temp_directory_path() / "trading_journal_writer_test";
  std::filesystem::create_directories(temp_dir);
  const auto path = temp_dir / "segment.journal";

  trading::journal::SegmentWriter writer(path);
  ASSERT_TRUE(writer.open().is_ok());

  trading::journal::v1::MarketQuotePayload quote{};
  quote.instrument_id = 1;
  quote.bid_ticks = 950;
  quote.ask_ticks = 951;
  quote.bid_size_lots = 2;
  quote.ask_size_lots = 3;
  quote.exchange_time_ns = 1'704'067'200'000'000'000ULL;

  const auto payload = trading::journal::v1::encode_market_quote(quote);
  const auto seq =
      writer.append(trading::journal::v1::RecordType::MarketQuote, payload, 100, 200);
  ASSERT_TRUE(seq.is_ok());
  EXPECT_EQ(seq.value(), 1U);
  ASSERT_TRUE(writer.flush().is_ok());
  writer.close();

  std::ifstream in(path, std::ios::binary);
  std::vector<std::uint8_t> file_bytes((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
  ASSERT_GE(file_bytes.size(), trading::journal::v1::kRecordHeaderSize);

  trading::journal::v1::RecordHeader header{};
  const auto header_span = std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(file_bytes.data()), file_bytes.size());
  ASSERT_TRUE(trading::journal::v1::decode_header(header_span, header));
  EXPECT_EQ(header.sequence, 1U);
  EXPECT_EQ(header.record_type, static_cast<std::uint16_t>(trading::journal::v1::RecordType::MarketQuote));

  const auto payload_span = header_span.subspan(trading::journal::v1::kRecordHeaderSize,
                                                 header.payload_length);
  trading::journal::v1::MarketQuotePayload decoded{};
  ASSERT_TRUE(trading::journal::v1::decode_market_quote(payload_span, decoded));
  EXPECT_EQ(decoded.instrument_id, 1U);
  EXPECT_EQ(decoded.bid_ticks, 950);
  EXPECT_EQ(decoded.ask_ticks, 951);
}
