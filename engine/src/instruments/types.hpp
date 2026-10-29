#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace trading::instruments {

enum class OptionType : std::uint8_t { None, Call, Put };

struct Instrument {
  std::string symbol;
  std::string venue;
  std::int64_t lot_size{};
  std::int64_t tick_size{};
  std::optional<std::string> expiry_yyyy_mm_dd;
  std::int64_t strike_ticks{};
  OptionType option_type{OptionType::None};
  std::string underlying;
};

struct InstrumentMaster {
  std::vector<Instrument> instruments;
  std::unordered_map<std::string, std::size_t> index_by_symbol;
};

} // namespace trading::instruments
