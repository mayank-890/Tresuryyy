#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "trading/core/result.hpp"
#include "trading/journal/generated/records.hpp"

namespace trading::journal {

enum class WriterError : std::uint8_t {
  NotOpen,
  IoError,
  EncodeError,
};

class SegmentWriter {
public:
  explicit SegmentWriter(std::filesystem::path path);

  [[nodiscard]] core::Result<void, WriterError> open();
  [[nodiscard]] core::Result<std::uint64_t, WriterError> append(
      v1::RecordType record_type, std::span<const std::byte> payload, std::uint64_t monotonic_ns,
      std::uint64_t wall_ns);
  [[nodiscard]] core::Result<void, WriterError> flush();
  void close();

  /// Writes segment footer and closes the fd; segment must not be appended again.
  [[nodiscard]] core::Result<void, WriterError> finalize_segment(std::uint64_t first_sequence,
                                                                 std::uint64_t record_count);

  [[nodiscard]] std::uint64_t next_sequence() const { return next_sequence_; }
  [[nodiscard]] std::uint64_t bytes_appended() const { return bytes_appended_; }
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
  std::filesystem::path path_;
  int fd_{-1};
  std::uint64_t next_sequence_{1};
  std::uint64_t bytes_appended_{0};
  bool finalized_{false};
};

[[nodiscard]] std::vector<std::byte> encode_record(
    v1::RecordType record_type, std::span<const std::byte> payload, std::uint64_t sequence,
    std::uint64_t monotonic_ns, std::uint64_t wall_ns);

} // namespace trading::journal
