#include <string>

#include <gtest/gtest.h>

#include "trading/marketdata/delta_normalize.hpp"

TEST(DeltaNormalize, QuoteFixtureToTicks) {
  trading::marketdata::InstrumentSpec spec{};
  spec.instrument_id = 1;
  spec.tick_size = 100;
  spec.lot_size = 1;
  spec.symbol = "SAMPLE-BTC-PERP";

  const std::string json =
      R"({"type":"quote","symbol":"SAMPLE-BTC-PERP","bid":"95000","ask":"95100","bid_size":"2","ask_size":"3","timestamp_us":1704067200000000})";

  const auto normalized = trading::marketdata::parse_delta_public_message(json, spec);
  ASSERT_TRUE(normalized.has_value());
  EXPECT_EQ(normalized->payload.instrument_id, 1U);
  EXPECT_EQ(normalized->payload.bid_ticks, 950);
  EXPECT_EQ(normalized->payload.ask_ticks, 951);
  EXPECT_EQ(normalized->payload.bid_size_lots, 2);
  EXPECT_EQ(normalized->payload.ask_size_lots, 3);
  EXPECT_EQ(normalized->payload.exchange_time_ns, 1'704'067'200'000'000'000ULL);
}

TEST(DeltaNormalize, RejectsWrongSymbol) {
  trading::marketdata::InstrumentSpec spec{};
  spec.instrument_id = 1;
  spec.tick_size = 100;
  spec.lot_size = 1;
  spec.symbol = "SAMPLE-BTC-PERP";

  const std::string json =
      R"({"type":"quote","symbol":"OTHER","bid":"1","ask":"2","bid_size":"1","ask_size":"1"})";
  EXPECT_FALSE(trading::marketdata::parse_delta_public_message(json, spec).has_value());
}
