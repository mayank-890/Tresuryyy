#include "trading/marketdata/generic_frame.hpp"
#include "trading/marketdata/normalizer.hpp"

#include <vector>

#include <gtest/gtest.h>

namespace {

using trading::journal::v1::Side;
using trading::marketdata::InstrumentHealth;
using trading::marketdata::MarketDataNormalizer;
using trading::marketdata::NormalizerConfig;
using trading::marketdata::Quote;
using trading::marketdata::ResyncPort;
using trading::marketdata::Trade;
using trading::marketdata::encode_quote_frame;
using trading::marketdata::encode_trade_frame;
using trading::marketdata::kDefaultStaleThresholdNs;
using trading::marketdata::parse_generic_frame;

struct ResyncProbe {
  std::vector<std::uint32_t> instruments;
};

void request_snapshot(void* ctx, const std::uint32_t instrument_id) {
  static_cast<ResyncProbe*>(ctx)->instruments.push_back(instrument_id);
}

Quote sample_quote(std::uint64_t sequence) {
  Quote quote{};
  quote.instrument_id = 42;
  quote.bid_ticks = 100;
  quote.ask_ticks = 101;
  quote.bid_size_lots = 2;
  quote.ask_size_lots = 3;
  quote.exchange_time_ns = 9'000;
  quote.sequence = sequence;
  return quote;
}

TEST(GenericFrame, NormalizesQuoteAndTrade) {
  Quote quote = sample_quote(1);
  const auto quote_bytes = encode_quote_frame(quote);
  const auto parsed_quote = parse_generic_frame(quote_bytes);
  ASSERT_EQ(parsed_quote.status, trading::marketdata::ParseStatus::Ok);
  ASSERT_TRUE(parsed_quote.is_quote);
  EXPECT_EQ(parsed_quote.quote.bid_ticks, 100);
  EXPECT_EQ(parsed_quote.quote.ask_ticks, 101);

  Trade trade{};
  trade.instrument_id = 42;
  trade.price_ticks = 100;
  trade.size_lots = 1;
  trade.side = Side::Buy;
  trade.exchange_time_ns = 8'000;
  trade.sequence = 2;
  const auto trade_bytes = encode_trade_frame(trade);
  const auto parsed_trade = parse_generic_frame(trade_bytes);
  ASSERT_EQ(parsed_trade.status, trading::marketdata::ParseStatus::Ok);
  ASSERT_FALSE(parsed_trade.is_quote);
  EXPECT_EQ(parsed_trade.trade.size_lots, 1);
  EXPECT_EQ(parsed_trade.trade.side, Side::Buy);
}

TEST(MarketDataNormalizer, MalformedFrameCountedAndDropped) {
  MarketDataNormalizer normalizer{};
  const std::array<std::byte, 8> bad = {std::byte{'X'}};
  const auto result = normalizer.ingest(bad, 1'000, 2'000);
  EXPECT_FALSE(result.accepted);
  EXPECT_TRUE(result.malformed);
  EXPECT_EQ(normalizer.stats().malformed_frames, 1u);
  EXPECT_FALSE(normalizer.orders_allowed(42));
}

TEST(MarketDataNormalizer, SequenceGapRequestsResyncAndBlocksOrders) {
  ResyncProbe probe{};
  ResyncPort port{};
  port.ctx = &probe;
  port.request_snapshot = request_snapshot;

  MarketDataNormalizer normalizer{};
  normalizer.set_resync_port(port);

  const auto frame1 = encode_quote_frame(sample_quote(1));
  ASSERT_TRUE(normalizer.ingest(frame1, 1'000, 2'000).accepted);
  EXPECT_TRUE(normalizer.orders_allowed(42));

  const auto frame3 = encode_quote_frame(sample_quote(3));
  const auto gap = normalizer.ingest(frame3, 1'100, 2'100);
  EXPECT_FALSE(gap.accepted);
  EXPECT_TRUE(gap.sequence_gap);
  EXPECT_EQ(normalizer.health(42), InstrumentHealth::Resyncing);
  EXPECT_FALSE(normalizer.orders_allowed(42));
  ASSERT_EQ(probe.instruments.size(), 1u);
  EXPECT_EQ(probe.instruments[0], 42u);

  const auto frame4 = encode_quote_frame(sample_quote(4));
  EXPECT_FALSE(normalizer.ingest(frame4, 1'200, 2'200).accepted);

  normalizer.apply_snapshot(42, 3);
  EXPECT_EQ(normalizer.health(42), InstrumentHealth::Healthy);
  const auto frame4b = encode_quote_frame(sample_quote(4));
  EXPECT_TRUE(normalizer.ingest(frame4b, 1'300, 2'300).accepted);
  EXPECT_TRUE(normalizer.orders_allowed(42));
}

TEST(MarketDataNormalizer, StalenessBlocksOrdersUntilFreshUpdate) {
  NormalizerConfig config{};
  config.stale_threshold_ns = 500'000'000;
  MarketDataNormalizer normalizer{};
  normalizer.set_config(config);
  normalizer.set_in_session(true);

  const auto frame1 = encode_quote_frame(sample_quote(1));
  ASSERT_TRUE(normalizer.ingest(frame1, 1'000'000'000, 2'000).accepted);
  EXPECT_TRUE(normalizer.orders_allowed(42));

  normalizer.tick(1'000'000'000 + config.stale_threshold_ns + 1);
  EXPECT_EQ(normalizer.health(42), InstrumentHealth::Stale);
  EXPECT_FALSE(normalizer.orders_allowed(42));

  const auto frame2 = encode_quote_frame(sample_quote(2));
  ASSERT_TRUE(normalizer.ingest(frame2, 1'700'000'000, 2'100).accepted);
  EXPECT_EQ(normalizer.health(42), InstrumentHealth::Healthy);
  EXPECT_TRUE(normalizer.orders_allowed(42));
}

TEST(MarketDataNormalizer, NoStaleOutsideSession) {
  MarketDataNormalizer normalizer{};
  normalizer.set_in_session(false);
  const auto frame1 = encode_quote_frame(sample_quote(1));
  ASSERT_TRUE(normalizer.ingest(frame1, 1'000, 2'000).accepted);

  normalizer.tick(1'000 + kDefaultStaleThresholdNs + 1);
  EXPECT_NE(normalizer.health(42), InstrumentHealth::Stale);
}

} // namespace
