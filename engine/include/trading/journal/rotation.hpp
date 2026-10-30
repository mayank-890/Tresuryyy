#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "trading/core/result.hpp"
#include "trading/journal/generated/records.hpp"
#include "trading/journal/writer.hpp"

namespace trading::journal {

/// DESIGN §10.2 default segment size; override in tests or profile (TODO(operator)).
inline constexpr std::uint64_t kDefaultMaxSegmentBytes = 256ULL * 1024ULL * 1024ULL;

struct RotationPolicy {
  std::uint64_t max_segment_bytes{kDefaultMaxSegmentBytes};
  /// Wall-clock age of the active segment; 0 disables time-based rotation.
  std::uint64_t max_segment_age_ns{0};
};

struct RetentionPolicy {
  /// Closed segments to retain; 0 keeps all closed segments.
  std::size_t max_closed_segments{0};
  /// Drop closed segments older than this (mtime); 0 disables age pruning.
  std::uint64_t max_closed_age_ns{0};
};

enum class RotationError : std::uint8_t {
  NotOpen,
  IoError,
  EncodeError,
};

struct SegmentInfo {
  std::filesystem::path path;
  std::uint64_t first_sequence{0};
  bool closed{false};
  std::uint64_t record_count{0};
  std::uint64_t last_sequence{0};
  std::uint64_t size_bytes{0};
};

enum class RetentionError : std::uint8_t {
  ArchiveNotVerified,
  IoError,
};

struct RetentionApplyResult {
  std::size_t candidates{0};
  std::size_t removed{0};
  bool archive_verified{false};
  std::vector<std::filesystem::path> refused_paths;
};

class RotatingJournalWriter {
public:
  RotatingJournalWriter(std::filesystem::path journal_root, RotationPolicy rotation);

  [[nodiscard]] core::Result<void, RotationError> open();
  [[nodiscard]] core::Result<std::uint64_t, RotationError> append(
      v1::RecordType record_type, std::span<const std::byte> payload, std::uint64_t monotonic_ns,
      std::uint64_t wall_ns);
  [[nodiscard]] core::Result<void, RotationError> flush();
  [[nodiscard]] core::Result<void, RotationError> rotate();
  void shutdown();

  [[nodiscard]] std::uint64_t next_sequence() const { return next_sequence_; }
  [[nodiscard]] const std::filesystem::path& active_path() const { return active_path_; }

private:
  [[nodiscard]] core::Result<void, RotationError> open_new_segment();
  [[nodiscard]] core::Result<void, RotationError> finalize_active();
  [[nodiscard]] bool should_rotate(std::uint64_t wall_ns) const;

  std::filesystem::path journal_root_;
  RotationPolicy rotation_;
  std::optional<SegmentWriter> writer_;
  std::filesystem::path active_path_;
  std::uint64_t next_sequence_{1};
  std::uint64_t segment_first_sequence_{0};
  std::uint64_t segment_record_count_{0};
  std::uint64_t segment_bytes_{0};
  std::uint64_t segment_opened_wall_ns_{0};
  bool open_{false};
};

[[nodiscard]] std::vector<SegmentInfo> scan_journal_segments(const std::filesystem::path& root);

[[nodiscard]] bool archive_verified_flag_set();

[[nodiscard]] core::Result<RetentionApplyResult, RetentionError> apply_retention(
    const std::filesystem::path& root, const RetentionPolicy& policy,
    bool archive_verified_override = false);

[[nodiscard]] std::filesystem::path segment_path_for_first_sequence(
    const std::filesystem::path& root, std::uint64_t first_sequence);

} // namespace trading::journal
