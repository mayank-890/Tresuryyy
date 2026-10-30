#include "enrich.hpp"
#include "instrument_load.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#ifndef TRADING_REPO_ROOT
#error "TRADING_REPO_ROOT must be defined"
#endif

namespace fs = std::filesystem;

namespace {

fs::path repo_root() { return fs::path(TRADING_REPO_ROOT); }

} // namespace

TEST(InstrumentEnrichTests, MergePatchAddsInstrumentId) {
  const auto seed = trading::instruments::load_instrument_master_file(
      repo_root() / "config/instruments/delta_crypto.toml");
  ASSERT_TRUE(seed);
  const auto patch = trading::instruments::load_instrument_master_file(
      repo_root() / "config/instruments/delta_recording_ids.toml");
  ASSERT_TRUE(patch);

  const auto merged =
      trading::instruments::merge_masters_by_symbol(seed.value(), patch.value());
  ASSERT_TRUE(merged);
  const auto* btc =
      trading::instruments::find_by_symbol(merged.value(), "SAMPLE-BTC-PERP");
  ASSERT_NE(btc, nullptr);
  ASSERT_TRUE(btc->instrument_id.has_value());
  EXPECT_EQ(*btc->instrument_id, 1U);
  EXPECT_EQ(
      trading::instruments::find_by_instrument_id(merged.value(), 1)->symbol,
      "SAMPLE-BTC-PERP");
}

TEST(InstrumentEnrichTests, ReportDetectsMissingSymbol) {
  const auto master = trading::instruments::load_instrument_master_file(
      repo_root() / "config/instruments/delta_recording_ids.toml");
  ASSERT_TRUE(master);
  const auto report = trading::instruments::build_enrichment_report(
      master.value(), {"SAMPLE-BTC-PERP", "UNKNOWN-SYMBOL"});
  ASSERT_TRUE(report);
  EXPECT_EQ(report.value().missing_symbols.size(), 1U);
  EXPECT_EQ(report.value().missing_symbols.front(), "UNKNOWN-SYMBOL");
}

TEST(InstrumentEnrichTests, RejectsDuplicateInstrumentId) {
  const fs::path path =
      fs::temp_directory_path() / "trading_instrument_dup_id_test.toml";
  std::ofstream out(path);
  out << R"(# SAMPLE — NOT FOR TRADING

[[instrument]]
instrument_id = 1
symbol = "A"
venue = "sample"
lot_size = 1
tick_size = 1
underlying = "X"

[[instrument]]
instrument_id = 1
symbol = "B"
venue = "sample"
lot_size = 1
tick_size = 1
underlying = "Y"
)";
  out.close();

  const auto loaded = trading::instruments::load_instrument_master_file(path);
  ASSERT_FALSE(loaded);
  EXPECT_EQ(loaded.error().code, trading::instruments::InstrumentErrorCode::Validation);
}

TEST(InstrumentEnrichTests, JournalSymbolFixtureFullyMaps) {
  const auto seed = trading::instruments::load_instrument_master_file(
      repo_root() / "config/instruments/delta_crypto.toml");
  ASSERT_TRUE(seed);
  const auto patch = trading::instruments::load_instrument_master_file(
      repo_root() / "config/instruments/delta_recording_ids.toml");
  ASSERT_TRUE(patch);
  const auto merged =
      trading::instruments::merge_masters_by_symbol(seed.value(), patch.value());
  ASSERT_TRUE(merged);

  const auto symbols = trading::instruments::load_symbol_list_file(
      repo_root() / "tests/fixtures/instruments/journal_symbols.txt");
  ASSERT_TRUE(symbols);

  const auto report =
      trading::instruments::build_enrichment_report(merged.value(), symbols.value());
  ASSERT_TRUE(report);
  EXPECT_TRUE(report.value().missing_symbols.empty());
  EXPECT_TRUE(report.value().missing_instrument_id.empty());
  EXPECT_EQ(report.value().mapped_symbols.size(), 2U);
}
