#include "trading/journal/segment_footer.hpp"

#include <array>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "trading/journal/generated/records.hpp"

namespace trading::journal {

namespace {

void put16(std::array<std::byte, kSegmentFooterSize>& out, std::size_t off, std::uint16_t v) {
  for (int i = 0; i < 2; ++i) {
    out[off + static_cast<std::size_t>(i)] = static_cast<std::byte>((v >> (8 * i)) & 0xFF);
  }
}

void put32(std::array<std::byte, kSegmentFooterSize>& out, std::size_t off, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    out[off + static_cast<std::size_t>(i)] = static_cast<std::byte>((v >> (8 * i)) & 0xFF);
  }
}

void put64(std::array<std::byte, kSegmentFooterSize>& out, std::size_t off, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out[off + static_cast<std::size_t>(i)] = static_cast<std::byte>((v >> (8 * i)) & 0xFF);
  }
}

std::uint16_t get16(std::span<const std::byte> data, std::size_t off) {
  std::uint16_t v = 0;
  for (int i = 1; i >= 0; --i) {
    v = static_cast<std::uint16_t>(v << 8 | static_cast<std::uint16_t>(data[off + static_cast<std::size_t>(i)]));
  }
  return v;
}

std::uint32_t get32(std::span<const std::byte> data, std::size_t off) {
  std::uint32_t v = 0;
  for (int i = 3; i >= 0; --i) {
    v = v << 8 | static_cast<std::uint32_t>(data[off + static_cast<std::size_t>(i)]);
  }
  return v;
}

std::uint64_t get64(std::span<const std::byte> data, std::size_t off) {
  std::uint64_t v = 0;
  for (int i = 7; i >= 0; --i) {
    v = v << 8 | static_cast<std::uint64_t>(data[off + static_cast<std::size_t>(i)]);
  }
  return v;
}

} // namespace

std::vector<std::byte> encode_segment_footer(const SegmentFooter& footer) {
  std::array<std::byte, kSegmentFooterSize> fixed{};
  std::memcpy(fixed.data(), kSegmentFooterMagic, 4);
  put16(fixed, 4, kSegmentFooterVersion);
  put16(fixed, 6, 0);
  put64(fixed, 8, footer.record_count);
  put64(fixed, 16, footer.last_sequence);
  put32(fixed, 24, footer.body_crc32c);
  put32(fixed, 28, 0);
  const std::uint32_t footer_crc = v1::compute_crc32c(std::span<const std::byte>(fixed.data(), 28));
  put32(fixed, 28, footer_crc);
  return std::vector<std::byte>(fixed.begin(), fixed.end());
}

core::Result<SegmentFooter, FooterError> decode_segment_footer(
    const std::span<const std::byte> footer_bytes) {
  if (footer_bytes.size() != kSegmentFooterSize) {
    return core::Result<SegmentFooter, FooterError>::err(FooterError::InvalidFooter);
  }
  if (std::memcmp(footer_bytes.data(), kSegmentFooterMagic, 4) != 0) {
    return core::Result<SegmentFooter, FooterError>::err(FooterError::InvalidFooter);
  }
  if (get16(footer_bytes, 4) != kSegmentFooterVersion) {
    return core::Result<SegmentFooter, FooterError>::err(FooterError::InvalidFooter);
  }
  std::array<std::byte, kSegmentFooterSize> temp{};
  std::memcpy(temp.data(), footer_bytes.data(), kSegmentFooterSize);
  put32(temp, 28, 0);
  const std::uint32_t computed = v1::compute_crc32c(std::span<const std::byte>(temp.data(), 28));
  const std::uint32_t stored_crc = get32(footer_bytes, 28);
  if (computed != stored_crc) {
    return core::Result<SegmentFooter, FooterError>::err(FooterError::InvalidFooter);
  }
  SegmentFooter out{};
  out.record_count = get64(footer_bytes, 8);
  out.last_sequence = get64(footer_bytes, 16);
  out.body_crc32c = get32(footer_bytes, 24);
  out.first_sequence = 0;
  return core::Result<SegmentFooter, FooterError>::ok(out);
}

core::Result<SegmentFooter, FooterError> read_segment_footer(const std::filesystem::path& path) {
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return core::Result<SegmentFooter, FooterError>::err(FooterError::IoError);
  }
  const off_t end = ::lseek(fd, 0, SEEK_END);
  if (end < 0 || static_cast<std::size_t>(end) < kSegmentFooterSize) {
    ::close(fd);
    return core::Result<SegmentFooter, FooterError>::err(FooterError::TooSmall);
  }
  std::array<std::byte, kSegmentFooterSize> buf{};
  if (::lseek(fd, end - static_cast<off_t>(kSegmentFooterSize), SEEK_SET) < 0) {
    ::close(fd);
    return core::Result<SegmentFooter, FooterError>::err(FooterError::IoError);
  }
  const ssize_t n = ::read(fd, buf.data(), kSegmentFooterSize);
  ::close(fd);
  if (n != static_cast<ssize_t>(kSegmentFooterSize)) {
    return core::Result<SegmentFooter, FooterError>::err(FooterError::IoError);
  }
  return decode_segment_footer(buf);
}

bool segment_has_footer(const std::filesystem::path& path) {
  return read_segment_footer(path).is_ok();
}

} // namespace trading::journal
