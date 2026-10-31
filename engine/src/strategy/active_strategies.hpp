#pragma once

#include "strategies/flat_echo/strategy.hpp"
#include "trading/strategy/registry.hpp"

namespace trading::strategy {

using ActiveStrategies = CompiledRegistry<flat_echo::FlatEcho>;

// Duplicate or empty names fail the build.
static_assert(ActiveStrategies::names_unique(), "duplicate strategy names fail the build");

} // namespace trading::strategy

#define TRADING_REGISTER_STRATEGY(Type)                                                            \
  static_assert(::trading::strategy::ActiveStrategies::template contains<Type>(),                 \
                "list the strategy in ActiveStrategies before registering it")
