#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#include "trading/journal/generated/records.hpp"
#include "trading/journal/rotation.hpp"
#include "trading/journal/segment_footer.hpp"

namespace {

std::vector<std::byte> quote_payload() {
  trading::journal::v1::MarketQuotePayload quote{};
  quote.instrument_id = 1;
  quote.bid_ticks = 100;
  quote.ask_ticks = 101;
  const auto raw = trading::journal::v1::encode_market_quote(quote);
  std::vector<std::byte> out;
  out.reserve(raw.size());
  for (const auto b : raw) {
    out.push_back(static_cast<std::byte>(b));
  }
  return out;
}

std::filesystem::path temp_journal_root() {
  const auto root =
      std::filesystem::temp_directory_path() / "trading_journal_rotation_test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  return root;
}

} // namespace

TEST(JournalRotation, RotatesAtByteLimitAndContinuesSequence) {
  const auto root = temp_journal_root();
  trading::journal::RotationPolicy policy{};
  policy.max_segment_bytes = 400;
  policy.max_segment_age_ns = 0;

  trading::journal::RotatingJournalWriter writer(root, policy);
  ASSERT_TRUE(writer.open().is_ok());

  const auto payload = quote_payload();
  std::uint64_t last_seq = 0;
  for (int i = 0; i < 8; ++i) {
    const auto seq = writer.append(trading::journal::v1::RecordType::MarketQuote, payload, 10,
                                   static_cast<std::uint64_t>(1'000 + i));
    ASSERT_TRUE(seq.is_ok());
    last_seq = seq.value();
  }
  const auto next_seq = writer.next_sequence();
  writer.shutdown();

  const auto segments = trading::journal::scan_journal_segments(root);
  ASSERT_GE(segments.size(), 2U);
  EXPECT_TRUE(segments.front().closed);
  EXPECT_EQ(segments.front().last_sequence + 1, segments[1].first_sequence);
  EXPECT_EQ(last_seq + 1, next_seq);
}

TEST(JournalRotation, ShutdownFinalizesActiveSegment) {
  const auto root = temp_journal_root();
  trading::journal::RotationPolicy policy{};
  policy.max_segment_bytes = 256 * 1024 * 1024;

  trading::journal::RotatingJournalWriter writer(root, policy);
  ASSERT_TRUE(writer.open().is_ok());
  const auto payload = quote_payload();
  ASSERT_TRUE(
      writer.append(trading::journal::v1::RecordType::MarketQuote, payload, 1, 2).is_ok());
  writer.shutdown();

  const auto segments = trading::journal::scan_journal_segments(root);
  ASSERT_EQ(segments.size(), 1U);
  EXPECT_TRUE(segments[0].closed);
  EXPECT_EQ(segments[0].record_count, 1U);
  EXPECT_TRUE(trading::journal::segment_has_footer(segments[0].path));
}

TEST(JournalRetention, RefusesDropWithoutArchiveVerified) {
  const auto root = temp_journal_root();
  trading::journal::RotationPolicy policy{};
  policy.max_segment_bytes = 200;

  trading::journal::RotatingJournalWriter writer(root, policy);
  ASSERT_TRUE(writer.open().is_ok());
  const auto payload = quote_payload();
  for (int i = 0; i < 6; ++i) {
    ASSERT_TRUE(
        writer.append(trading::journal::v1::RecordType::MarketQuote, payload, 1, 10).is_ok());
  }
  writer.shutdown();
  EXPECT_GE(trading::journal::scan_journal_segments(root).size(), 2U);

  trading::journal::RetentionPolicy retention{};
  retention.max_closed_segments = 1;
  const auto applied = trading::journal::apply_retention(root, retention, false);
  ASSERT_TRUE(applied.is_ok());
  EXPECT_FALSE(applied.value().archive_verified);
  EXPECT_EQ(applied.value().removed, 0U);
  EXPECT_GE(applied.value().candidates, 1U);
  EXPECT_GE(trading::journal::scan_journal_segments(root).size(), 2U);
}

TEST(JournalRetention, DropsWhenArchiveVerified) {
  const auto root = temp_journal_root();
  trading::journal::RotationPolicy policy{};
  policy.max_segment_bytes = 200;

  trading::journal::RotatingJournalWriter writer(root, policy);
  ASSERT_TRUE(writer.open().is_ok());
  const auto payload = quote_payload();
  for (int i = 0; i < 6; ++i) {
    ASSERT_TRUE(
        writer.append(trading::journal::v1::RecordType::MarketQuote, payload, 1, 10).is_ok());
  }
  writer.shutdown();

  trading::journal::RetentionPolicy retention{};
  retention.max_closed_segments = 1;
  const auto applied = trading::journal::apply_retention(root, retention, true);
  ASSERT_TRUE(applied.is_ok());
  EXPECT_GE(applied.value().removed, 1U);
  const auto remaining = trading::journal::scan_journal_segments(root);
  EXPECT_LE(remaining.size(), 2U);
}
