#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "active_strategies.hpp"
#include "strategies/flat_echo/strategy.hpp"
#include "trading/strategy/param_buffer.hpp"
#include "trading/strategy/registry.hpp"

namespace {

using trading::strategy::MarketSnapshot;
using trading::strategy::OrderIntent;
using trading::strategy::ParamDoubleBuffer;
using trading::strategy::ParamView;
using trading::strategy::Quote;
using trading::strategy::Side;
using trading::strategy::TimeInForce;
using trading::strategy::flat_echo::FlatEcho;
using trading::strategy::flat_echo::FlatParams;
using trading::strategy::flat_echo::size_lots;
using trading::strategy::param_view;

struct Versioned {
  std::uint64_t version{0};
  int value{0};
};

struct DupA {
  static constexpr std::string_view kName{"dup"};
};
struct DupB {
  static constexpr std::string_view kName{"dup"};
};
struct OtherName {
  static constexpr std::string_view kName{"other"};
};
struct EmptyName {
  static constexpr std::string_view kName{};
};
struct NotRegistered {
  static constexpr std::string_view kName{"nope"};
};

template <typename T>
concept HasClientOrderId = requires(T value) { value.client_order_id; };

static_assert(!HasClientOrderId<OrderIntent>);
static_assert(trading::strategy::ActiveStrategies::names_unique());
static_assert(trading::strategy::ActiveStrategies::contains<FlatEcho>());
static_assert(!trading::strategy::ActiveStrategies::contains<NotRegistered>());
static_assert(!trading::strategy::CompiledRegistry<DupA, DupB>::names_unique());
static_assert(trading::strategy::CompiledRegistry<DupA, OtherName>::names_unique());
static_assert(!trading::strategy::CompiledRegistry<EmptyName>::names_unique());

bool include_line_is_forbidden(std::string_view line) {
  const auto include = line.find("#include");
  if (include == std::string_view::npos) {
    return false;
  }
  const auto rest = line.substr(include);
  return rest.find("broker") != std::string_view::npos ||
         rest.find("journal") != std::string_view::npos ||
         rest.find("socket") != std::string_view::npos ||
         rest.find("asio") != std::string_view::npos;
}

FlatParams sample_params() {
  FlatParams params{};
  params.version = 4;
  params.instrument_id = 42;
  params.qty_lots = 2;
  params.side = Side::Buy;
  params.time_in_force = TimeInForce::ImmediateOrCancel;
  return params;
}

MarketSnapshot sample_market() {
  static const Quote quotes[] = {
      Quote{.instrument_id = 99, .bid_ticks = 1, .ask_ticks = 2},
      Quote{.instrument_id = 42, .bid_ticks = 10, .ask_ticks = 12},
  };
  return MarketSnapshot{.sequence = 99, .quotes = quotes};
}

} // namespace

TEST(ParamDoubleBuffer, CopyStaysFixedUntilNextCycle) {
  ParamDoubleBuffer<Versioned> buffer;
  const auto first = buffer.load_for_cycle();
  EXPECT_EQ(first.version, 0u);
  EXPECT_EQ(first.value, 0);

  buffer.publish(Versioned{1, 10});
  EXPECT_EQ(first.version, 0u);
  EXPECT_EQ(first.value, 0);

  const auto second = buffer.load_for_cycle();
  EXPECT_EQ(second.version, 1u);
  EXPECT_EQ(second.value, 10);

  buffer.publish(Versioned{2, 20});
  EXPECT_EQ(second.version, 1u);
  EXPECT_EQ(second.value, 10);
  EXPECT_EQ(buffer.load_for_cycle().version, 2u);
  EXPECT_EQ(buffer.load_for_cycle().value, 20);
}

TEST(FlatEchoSizing, NonPositiveQtyIsZero) {
  FlatParams params{};
  EXPECT_EQ(size_lots(params), 0);
  params.qty_lots = -3;
  EXPECT_EQ(size_lots(params), 0);
  params.qty_lots = 5;
  EXPECT_EQ(size_lots(params), 5);
}

TEST(FlatEcho, EmitsBuyAtAskUsingPublishedVersion) {
  const FlatEcho strategy{7};
  const auto params = sample_params();
  const auto market = sample_market();
  std::array<OrderIntent, 2> out{};

  const auto written = strategy.decide(market, param_view(params), out);
  ASSERT_EQ(written, 1u);
  EXPECT_EQ(out[0].strategy_id, 7u);
  EXPECT_EQ(out[0].instrument_id, 42u);
  EXPECT_EQ(out[0].side, Side::Buy);
  EXPECT_EQ(out[0].qty_lots, 2);
  EXPECT_EQ(out[0].price_ticks, 12);
  EXPECT_EQ(out[0].time_in_force, TimeInForce::ImmediateOrCancel);
  EXPECT_EQ(out[0].param_version, 4u);
  EXPECT_EQ(out[0].snapshot_sequence, 99u);
  EXPECT_EQ(out[1].qty_lots, 0);

  std::array<OrderIntent, 1> again{};
  EXPECT_EQ(strategy.decide(market, param_view(params), again), 1u);
  EXPECT_EQ(again[0].price_ticks, out[0].price_ticks);
}

TEST(FlatEcho, EmitsSellAtBid) {
  FlatEcho strategy{7};
  auto params = sample_params();
  params.side = Side::Sell;
  std::array<OrderIntent, 1> out{};
  ASSERT_EQ(strategy.decide(sample_market(), param_view(params), out), 1u);
  EXPECT_EQ(out[0].side, Side::Sell);
  EXPECT_EQ(out[0].price_ticks, 10);
}

TEST(FlatEcho, EmitsNothingWithoutASignal) {
  FlatEcho strategy{7};
  const auto market = sample_market();
  std::array<OrderIntent, 1> out{};
  out[0].qty_lots = 8;

  auto params = sample_params();
  params.qty_lots = 0;
  EXPECT_EQ(strategy.decide(market, param_view(params), out), 0u);
  EXPECT_EQ(out[0].qty_lots, 8);

  params.qty_lots = -1;
  EXPECT_EQ(strategy.decide(market, param_view(params), out), 0u);

  params = sample_params();
  params.instrument_id = 7;
  EXPECT_EQ(strategy.decide(market, param_view(params), out), 0u);

  params = sample_params();
  params.side = static_cast<Side>(0);
  EXPECT_EQ(strategy.decide(market, param_view(params), out), 0u);

  const std::span<OrderIntent> none;
  params = sample_params();
  EXPECT_EQ(strategy.decide(market, param_view(params), none), 0u);

  auto view = param_view(params);
  view.version = params.version + 1;
  EXPECT_EQ(strategy.decide(market, view, out), 0u);

  const std::byte junk{std::byte{1}};
  const ParamView short_view{params.version, std::span<const std::byte>(&junk, 1)};
  EXPECT_EQ(strategy.decide(market, short_view, out), 0u);
}

TEST(StrategyRegistry, ListsFlatEchoAndRejectsMissingNames) {
  const auto listed = trading::strategy::registered_strategies();
  ASSERT_EQ(listed.size(), 1u);
  EXPECT_EQ(listed[0].name, FlatEcho::kName);
  EXPECT_EQ(listed[0].strategy_id, trading::strategy::flat_echo::kSampleStrategyId);

  const auto* found = trading::strategy::find_strategy("flat_echo");
  ASSERT_NE(found, nullptr);
  ASSERT_NE(found->create, nullptr);
  const auto* strategy = found->create();
  ASSERT_NE(strategy, nullptr);
  EXPECT_EQ(strategy->name(), "flat_echo");
  EXPECT_EQ(strategy->strategy_id(), trading::strategy::flat_echo::kSampleStrategyId);

  std::array<OrderIntent, 1> out{};
  const auto params = sample_params();
  EXPECT_EQ(strategy->decide(sample_market(), param_view(params), out), 1u);
  EXPECT_EQ(out[0].param_version, params.version);

  EXPECT_EQ(trading::strategy::find_strategy("missing"), nullptr);
  EXPECT_TRUE(trading::strategy::ActiveStrategies::names_unique());
  EXPECT_EQ(trading::strategy::ActiveStrategies::count, 1u);
}

TEST(StrategySources, DoNotIncludeBrokerJournalOrSockets) {
  const std::filesystem::path root{TRADING_REPO_ROOT};
  const char* relative[] = {
      "engine/include/trading/strategy/intent.hpp",
      "engine/include/trading/strategy/market_snapshot.hpp",
      "engine/include/trading/strategy/param_buffer.hpp",
      "engine/include/trading/strategy/strategy.hpp",
      "engine/include/trading/strategy/registry.hpp",
      "engine/src/strategy/active_strategies.hpp",
      "engine/src/strategy/registry.cpp",
      "engine/src/strategy/strategies/flat_echo/strategy.hpp",
      "engine/src/strategy/strategies/flat_echo/strategy.cpp",
  };
  for (const char* rel : relative) {
    const auto path = root / rel;
    std::ifstream input{path};
    ASSERT_TRUE(input.is_open()) << path;
    std::string line;
    while (std::getline(input, line)) {
      EXPECT_FALSE(include_line_is_forbidden(line)) << path << ": " << line;
    }
  }
}
