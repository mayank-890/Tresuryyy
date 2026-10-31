#include "trading/strategy/registry.hpp"

#include "active_strategies.hpp"

namespace trading::strategy {
namespace {

IStrategy* create_flat_echo() {
  // SAMPLE id. Not a production assignment. TODO(operator)
  static flat_echo::FlatEcho instance{flat_echo::kSampleStrategyId};
  return &instance;
}

const RegisteredStrategy kFactories[] = {
    {flat_echo::FlatEcho::kName, flat_echo::kSampleStrategyId, &create_flat_echo},
};

} // namespace

std::span<const RegisteredStrategy> registered_strategies() { return kFactories; }

const RegisteredStrategy* find_strategy(std::string_view name) {
  for (const RegisteredStrategy& entry : kFactories) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

} // namespace trading::strategy
