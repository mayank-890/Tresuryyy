#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "trading/core/result.hpp"
#include "trading/journal/generated/records.hpp"
#include "trading/journal/recovery.hpp"

namespace trading::journal {

// DESIGN §10.2. The group-commit window is at most 2 ms. A size threshold
// is not specified; 0 disables size-based flush (TODO(operator)).
inline constexpr std::uint64_t kGroupCommitWindowNs = 2'000'000;
inline constexpr std::size_t kGroupCommitBufferBytes = 256 * 1024;

enum class GroupError : std::uint8_t {
  NotOpen = 1,
  IoError,
  Halted,
  NeedsCommit,
  EncodeError,
  ClosedSegment,
  FatalCorruption,
};

struct GroupCommitConfig {
  std::uint64_t window_ns{kGroupCommitWindowNs};
  std::size_t batch_capacity{kGroupCommitBufferBytes};
  std::size_t size_threshold{0};
  std::uint64_t initial_sequence{1};
};

// Test seam. Production uses the file descriptor. Neither call runs on append.
struct IoHooks {
  void* ctx{nullptr};
  bool (*write)(void* ctx, const std::byte* data, std::size_t size){nullptr};
  bool (*fsync)(void* ctx){nullptr};
};

struct OpenReport {
  RecoveryApply recovery{RecoveryApply::None};
  bool alert_al14{false};
  std::uint64_t records_kept{0};
  std::uint64_t last_sequence{0};
};

// Appends into a preallocated buffer. The durable sequence moves only after
// fsync returns. `send_allowed` is the NFR-REL-01 check for that sequence.
class GroupCommitJournal {
public:
  explicit GroupCommitJournal(std::filesystem::path path, GroupCommitConfig config = {});
  GroupCommitJournal(IoHooks hooks, GroupCommitConfig config);

  GroupCommitJournal(const GroupCommitJournal&) = delete;
  GroupCommitJournal& operator=(const GroupCommitJournal&) = delete;

  ~GroupCommitJournal();

  [[nodiscard]] core::Result<OpenReport, GroupError> open();
  [[nodiscard]] core::Result<std::uint64_t, GroupError> append(v1::RecordType record_type,
                                                               std::span<const std::byte> payload,
                                                               std::uint64_t monotonic_ns,
                                                               std::uint64_t wall_ns);
  [[nodiscard]] core::Result<void, GroupError> maybe_commit(std::uint64_t now_ns);
  [[nodiscard]] core::Result<void, GroupError> commit();

  [[nodiscard]] bool send_allowed(std::uint64_t sequence) const;
  [[nodiscard]] std::uint64_t durable_sequence() const { return durable_sequence_; }
  [[nodiscard]] std::uint64_t next_sequence() const { return next_sequence_; }
  [[nodiscard]] std::size_t buffered_bytes() const { return used_; }
  [[nodiscard]] bool halted() const { return halted_; }

  void close();

private:
  [[nodiscard]] bool write_all(const std::byte* data, std::size_t size);
  [[nodiscard]] bool fsync_all();

  std::filesystem::path path_;
  GroupCommitConfig config_{};
  IoHooks hooks_{};
  bool use_hooks_{false};
  int fd_{-1};
  bool open_{false};
  bool halted_{false};
  bool awaiting_fsync_{false};
  std::vector<std::byte> buffer_{};
  std::size_t used_{0};
  std::uint64_t next_sequence_{1};
  std::uint64_t durable_sequence_{0};
  std::uint64_t awaiting_sequence_{0};
  std::uint64_t batch_last_sequence_{0};
  std::uint64_t batch_started_ns_{0};
  bool batch_open_{false};
};

} // namespace trading::journal
