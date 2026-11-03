#include "trading/journal/group_commit.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace trading::journal {

namespace {

void put16(std::span<std::byte> dest, std::size_t offset, std::uint16_t value) {
  dest[offset] = static_cast<std::byte>(value & 0xFF);
  dest[offset + 1] = static_cast<std::byte>((value >> 8) & 0xFF);
}

void put32(std::span<std::byte> dest, std::size_t offset, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    dest[offset + static_cast<std::size_t>(i)] =
        static_cast<std::byte>((value >> (8 * i)) & 0xFF);
  }
}

void put64(std::span<std::byte> dest, std::size_t offset, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    dest[offset + static_cast<std::size_t>(i)] =
        static_cast<std::byte>((value >> (8 * i)) & 0xFF);
  }
}

std::size_t encode_into(std::span<std::byte> dest, v1::RecordType record_type,
                        std::span<const std::byte> payload, std::uint64_t sequence,
                        std::uint64_t monotonic_ns, std::uint64_t wall_ns) {
  if (payload.size() > 0xFFFFFFFFU) {
    return 0;
  }
  const std::size_t total = v1::kRecordHeaderSize + payload.size();
  if (dest.size() < total) {
    return 0;
  }
  dest[0] = std::byte{'T'};
  dest[1] = std::byte{'J'};
  dest[2] = std::byte{'N'};
  dest[3] = std::byte{'L'};
  put16(dest, 4, v1::kFormatVersion);
  put16(dest, 6, static_cast<std::uint16_t>(record_type));
  put64(dest, 8, sequence);
  put64(dest, 16, monotonic_ns);
  put64(dest, 24, wall_ns);
  put32(dest, 32, static_cast<std::uint32_t>(payload.size()));
  put32(dest, 36, 0);
  if (!payload.empty()) {
    std::memcpy(dest.data() + v1::kRecordHeaderSize, payload.data(), payload.size());
  }
  const std::uint32_t crc = v1::compute_crc32c(dest.first(total));
  put32(dest, 36, crc);
  return total;
}

bool read_entire(const std::filesystem::path& path, std::vector<std::byte>& out) {
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return false;
  }
  struct stat st {};
  if (::fstat(fd, &st) != 0 || st.st_size < 0) {
    ::close(fd);
    return false;
  }
  out.assign(static_cast<std::size_t>(st.st_size), std::byte{0});
  std::size_t got = 0;
  while (got < out.size()) {
    const ssize_t n = ::read(fd, reinterpret_cast<char*>(out.data()) + got, out.size() - got);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      ::close(fd);
      return false;
    }
    if (n == 0) {
      ::close(fd);
      return false;
    }
    got += static_cast<std::size_t>(n);
  }
  ::close(fd);
  return true;
}

} // namespace

GroupCommitJournal::GroupCommitJournal(std::filesystem::path path, GroupCommitConfig config)
    : path_(std::move(path)), config_(config), use_hooks_(false) {}

GroupCommitJournal::GroupCommitJournal(IoHooks hooks, GroupCommitConfig config)
    : config_(config), hooks_(hooks), use_hooks_(true) {}

GroupCommitJournal::~GroupCommitJournal() { close(); }

core::Result<OpenReport, GroupError> GroupCommitJournal::open() {
  if (open_) {
    OpenReport report;
    report.records_kept = durable_sequence_ == 0 ? 0 : durable_sequence_;
    report.last_sequence = durable_sequence_;
    return core::Result<OpenReport, GroupError>::ok(report);
  }
  if (config_.batch_capacity < v1::kRecordHeaderSize) {
    return core::Result<OpenReport, GroupError>::err(GroupError::EncodeError);
  }
  buffer_.assign(config_.batch_capacity, std::byte{0});
  used_ = 0;
  OpenReport report;
  if (use_hooks_) {
    if (hooks_.write == nullptr || hooks_.fsync == nullptr) {
      return core::Result<OpenReport, GroupError>::err(GroupError::IoError);
    }
    next_sequence_ = config_.initial_sequence == 0 ? 1 : config_.initial_sequence;
    durable_sequence_ = 0;
    open_ = true;
    return core::Result<OpenReport, GroupError>::ok(report);
  }

  const int existing = ::open(path_.c_str(), O_RDONLY);
  if (existing >= 0) {
    ::close(existing);
    const RecoveryResult recovered = recover_segment(path_, true);
    if (recovered.plan.closed_segment) {
      return core::Result<OpenReport, GroupError>::err(GroupError::ClosedSegment);
    }
    if (recovered.plan.kind == ScanKind::Fatal || recovered.action == RecoveryApply::Refused) {
      return core::Result<OpenReport, GroupError>::err(GroupError::FatalCorruption);
    }
    report.recovery = recovered.action;
    report.records_kept = recovered.plan.records;
    report.last_sequence = recovered.plan.last_sequence;
    if (recovered.action == RecoveryApply::Applied) {
      std::vector<std::byte> prefix;
      if (!read_entire(path_, prefix)) {
        return core::Result<OpenReport, GroupError>::err(GroupError::IoError);
      }
      const RecoveryPlan plan = plan_segment(prefix);
      if (plan.kind != ScanKind::Ok) {
        return core::Result<OpenReport, GroupError>::err(GroupError::IoError);
      }
      report.alert_al14 = true;
      report.records_kept = plan.records;
      report.last_sequence = plan.last_sequence;
    }
  }

  fd_ = ::open(path_.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
  if (fd_ < 0) {
    return core::Result<OpenReport, GroupError>::err(GroupError::IoError);
  }
  if (report.records_kept == 0) {
    next_sequence_ = 1;
    durable_sequence_ = 0;
  } else if (report.last_sequence == UINT64_MAX) {
    ::close(fd_);
    fd_ = -1;
    return core::Result<OpenReport, GroupError>::err(GroupError::Halted);
  } else {
    next_sequence_ = report.last_sequence + 1;
    durable_sequence_ = report.last_sequence;
  }
  open_ = true;
  return core::Result<OpenReport, GroupError>::ok(report);
}

core::Result<std::uint64_t, GroupError> GroupCommitJournal::append(
    const v1::RecordType record_type, const std::span<const std::byte> payload,
    const std::uint64_t monotonic_ns, const std::uint64_t wall_ns) {
  if (!open_) {
    return core::Result<std::uint64_t, GroupError>::err(GroupError::NotOpen);
  }
  if (halted_) {
    return core::Result<std::uint64_t, GroupError>::err(GroupError::Halted);
  }
  if (awaiting_fsync_) {
    return core::Result<std::uint64_t, GroupError>::err(GroupError::NeedsCommit);
  }
  if (next_sequence_ == 0) {
    return core::Result<std::uint64_t, GroupError>::err(GroupError::Halted);
  }
  const std::size_t need = v1::kRecordHeaderSize + payload.size();
  if (need < v1::kRecordHeaderSize || payload.size() > 0xFFFFFFFFU) {
    return core::Result<std::uint64_t, GroupError>::err(GroupError::EncodeError);
  }
  if (used_ + need > buffer_.size()) {
    if (used_ == 0) {
      return core::Result<std::uint64_t, GroupError>::err(GroupError::EncodeError);
    }
    return core::Result<std::uint64_t, GroupError>::err(GroupError::NeedsCommit);
  }
  const std::size_t written =
      encode_into(std::span<std::byte>(buffer_.data() + used_, buffer_.size() - used_), record_type,
                  payload, next_sequence_, monotonic_ns, wall_ns);
  if (written != need) {
    return core::Result<std::uint64_t, GroupError>::err(GroupError::EncodeError);
  }
  if (!batch_open_) {
    batch_started_ns_ = monotonic_ns;
    batch_open_ = true;
  }
  const std::uint64_t sequence = next_sequence_;
  batch_last_sequence_ = sequence;
  used_ += written;
  ++next_sequence_;
  return core::Result<std::uint64_t, GroupError>::ok(sequence);
}

core::Result<void, GroupError> GroupCommitJournal::maybe_commit(const std::uint64_t now_ns) {
  if (!open_) {
    return core::Result<void, GroupError>::err(GroupError::NotOpen);
  }
  if (halted_) {
    return core::Result<void, GroupError>::err(GroupError::Halted);
  }
  if (awaiting_fsync_) {
    return commit();
  }
  if (used_ == 0) {
    return core::Result<void, GroupError>::ok();
  }
  const bool size_ready = config_.size_threshold > 0 && used_ >= config_.size_threshold;
  const bool time_ready =
      now_ns >= batch_started_ns_ && (now_ns - batch_started_ns_) >= config_.window_ns;
  if (!size_ready && !time_ready) {
    return core::Result<void, GroupError>::ok();
  }
  return commit();
}

core::Result<void, GroupError> GroupCommitJournal::commit() {
  if (!open_) {
    return core::Result<void, GroupError>::err(GroupError::NotOpen);
  }
  if (halted_) {
    return core::Result<void, GroupError>::err(GroupError::Halted);
  }
  if (used_ == 0 && !awaiting_fsync_) {
    return core::Result<void, GroupError>::ok();
  }
  if (!awaiting_fsync_) {
    if (!write_all(buffer_.data(), used_)) {
      halted_ = true;
      return core::Result<void, GroupError>::err(GroupError::IoError);
    }
    awaiting_fsync_ = true;
    awaiting_sequence_ = batch_last_sequence_;
    used_ = 0;
    batch_open_ = false;
  }
  // Publish only after fsync returns. A caller observing durable_sequence
  // from inside the fsync hook still sees the previous value.
  if (!fsync_all()) {
    return core::Result<void, GroupError>::err(GroupError::IoError);
  }
  durable_sequence_ = awaiting_sequence_;
  awaiting_fsync_ = false;
  return core::Result<void, GroupError>::ok();
}

bool GroupCommitJournal::send_allowed(const std::uint64_t sequence) const {
  return open_ && !halted_ && sequence > 0 && sequence <= durable_sequence_;
}

void GroupCommitJournal::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  open_ = false;
}

bool GroupCommitJournal::write_all(const std::byte* data, const std::size_t size) {
  if (use_hooks_) {
    return hooks_.write(hooks_.ctx, data, size);
  }
  std::size_t wrote = 0;
  while (wrote < size) {
    const ssize_t n = ::write(fd_, reinterpret_cast<const char*>(data) + wrote, size - wrote);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      return false;
    }
    wrote += static_cast<std::size_t>(n);
  }
  return true;
}

bool GroupCommitJournal::fsync_all() {
  if (use_hooks_) {
    return hooks_.fsync(hooks_.ctx);
  }
  return fd_ >= 0 && ::fsync(fd_) == 0;
}

} // namespace trading::journal
