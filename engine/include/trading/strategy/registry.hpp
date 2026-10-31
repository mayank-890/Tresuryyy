#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace trading::strategy {

class IStrategy;

struct RegisteredStrategy {
  std::string_view name;
  std::uint32_t strategy_id;
  IStrategy* (*create)();
};

[[nodiscard]] std::span<const RegisteredStrategy> registered_strategies();
[[nodiscard]] const RegisteredStrategy* find_strategy(std::string_view name);

// Compile-time list. Duplicate or empty names make names_unique() false.
// ActiveStrategies static_asserts that, so a duplicate fails the build.
template <typename... Strategies> struct CompiledRegistry {
  static constexpr std::size_t count = sizeof...(Strategies);

  static constexpr bool names_unique() {
    if constexpr (sizeof...(Strategies) == 0) {
      return true;
    } else {
      constexpr std::array<std::string_view, sizeof...(Strategies)> names{Strategies::kName...};
      for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i].empty()) {
          return false;
        }
        for (std::size_t j = i + 1; j < names.size(); ++j) {
          if (names[i] == names[j]) {
            return false;
          }
        }
      }
      return true;
    }
  }

  template <typename T> static constexpr bool contains() {
    if constexpr (sizeof...(Strategies) == 0) {
      return false;
    } else {
      return ((std::is_same_v<T, Strategies>) || ...);
    }
  }
};

} // namespace trading::strategy
