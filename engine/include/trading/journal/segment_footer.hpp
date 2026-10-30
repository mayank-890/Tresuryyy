#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "trading/core/result.hpp"

namespace trading::journal {

inline constexpr char kSegmentFooterMagic[4] = {'T', 'J', 'N', 'F'};
inline constexpr std::uint16_t kSegmentFooterVersion = 1;
inline constexpr std::size_t kSegmentFooterSize = 32;

struct SegmentFooter {
  std::uint64_t record_count{};
  std::uint64_t last_sequence{};
  std::uint64_t first_sequence{};
  std::uint32_t body_crc32c{};
};

enum class FooterError : std::uint8_t {
  IoError,
  InvalidFooter,
  TooSmall,
};

[[nodiscard]] std::vector<std::byte> encode_segment_footer(const SegmentFooter& footer);

[[nodiscard]] core::Result<SegmentFooter, FooterError> decode_segment_footer(
    std::span<const std::byte> footer_bytes);

[[nodiscard]] core::Result<SegmentFooter, FooterError> read_segment_footer(
    const std::filesystem::path& path);

[[nodiscard]] bool segment_has_footer(const std::filesystem::path& path);

} // namespace trading::journal
