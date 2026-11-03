#include "trading/journal/recovery.hpp"

#include "trading/journal/generated/records.hpp"
#include "trading/journal/segment_footer.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace trading::journal {

namespace {

class Fd {
public:
  explicit Fd(int fd) : fd_(fd) {}
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  ~Fd() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  int get() const { return fd_; }
  int release() {
    const int fd = fd_;
    fd_ = -1;
    return fd;
  }

private:
  int fd_{-1};
};

void put_crc_zero(std::vector<std::byte>& record) {
  record[36] = std::byte{0};
  record[37] = std::byte{0};
  record[38] = std::byte{0};
  record[39] = std::byte{0};
}

bool magic_ok(const v1::RecordHeader& header) {
  return header.magic[0] == 'T' && header.magic[1] == 'J' && header.magic[2] == 'N' &&
         header.magic[3] == 'L';
}

bool crc_matches(std::span<const std::byte> data, std::size_t offset, std::uint32_t payload_length,
                 std::uint32_t expected) {
  const std::size_t total = v1::kRecordHeaderSize + static_cast<std::size_t>(payload_length);
  std::vector<std::byte> record(total);
  std::memcpy(record.data(), data.data() + offset, total);
  put_crc_zero(record);
  return v1::compute_crc32c(record) == expected;
}

RecoveryPlan fatal_at(ScanReason reason, std::uint64_t records, std::uint64_t last_sequence,
                      std::size_t offset) {
  RecoveryPlan plan;
  plan.kind = ScanKind::Fatal;
  plan.reason = reason;
  plan.records = records;
  plan.last_sequence = last_sequence;
  plan.valid_prefix = static_cast<std::uint64_t>(offset);
  plan.alert_al14 = false;
  return plan;
}

RecoveryPlan torn_at(ScanReason reason, std::uint64_t records, std::uint64_t last_sequence,
                     std::size_t offset, std::size_t size) {
  RecoveryPlan plan;
  plan.kind = ScanKind::TornTail;
  plan.reason = reason;
  plan.records = records;
  plan.last_sequence = last_sequence;
  plan.valid_prefix = static_cast<std::uint64_t>(offset);
  plan.torn_suffix = static_cast<std::uint64_t>(size - offset);
  plan.alert_al14 = true;
  return plan;
}

bool read_entire(const std::filesystem::path& path, std::vector<std::byte>& out) {
  Fd fd{::open(path.c_str(), O_RDONLY)};
  if (fd.get() < 0) {
    return false;
  }
  struct stat st {};
  if (::fstat(fd.get(), &st) != 0 || st.st_size < 0) {
    return false;
  }
  out.assign(static_cast<std::size_t>(st.st_size), std::byte{0});
  std::size_t got = 0;
  while (got < out.size()) {
    const ssize_t n = ::read(fd.get(), reinterpret_cast<char*>(out.data()) + got, out.size() - got);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      return false;
    }
    got += static_cast<std::size_t>(n);
  }
  return true;
}

bool write_durable(const std::filesystem::path& path, std::span<const std::byte> data) {
  Fd fd{::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644)};
  if (fd.get() < 0) {
    return false;
  }
  std::size_t wrote = 0;
  while (wrote < data.size()) {
    const ssize_t n =
        ::write(fd.get(), reinterpret_cast<const char*>(data.data()) + wrote, data.size() - wrote);
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
  return ::fsync(fd.get()) == 0;
}

void fsync_directory(const std::filesystem::path& directory) {
  Fd fd{::open(directory.c_str(), O_RDONLY)};
  if (fd.get() >= 0) {
    (void)::fsync(fd.get());
  }
}

std::filesystem::path backup_path_for(const std::filesystem::path& path) {
  return path.parent_path() / (path.filename().string() + ".torn");
}

std::filesystem::path temp_path_for(const std::filesystem::path& path) {
  return path.parent_path() / (path.filename().string() + ".recovering");
}

bool same_bytes(const std::filesystem::path& path, std::span<const std::byte> data) {
  std::vector<std::byte> got;
  if (!read_entire(path, got) || got.size() != data.size()) {
    return false;
  }
  if (data.empty()) {
    return true;
  }
  return std::memcmp(got.data(), data.data(), data.size()) == 0;
}

} // namespace

RecoveryPlan plan_segment(const std::span<const std::byte> data) {
  RecoveryPlan plan;
  plan.kind = ScanKind::Ok;
  plan.reason = ScanReason::Ok;
  plan.valid_prefix = static_cast<std::uint64_t>(data.size());
  std::size_t offset = 0;
  bool have_expected = false;
  std::uint64_t expected = 0;
  while (offset < data.size()) {
    const std::size_t remaining = data.size() - offset;
    if (remaining < v1::kRecordHeaderSize) {
      if (remaining == kSegmentFooterSize) {
        const auto footer = decode_segment_footer(data.subspan(offset, remaining));
        if (footer.is_ok()) {
          plan.kind = ScanKind::ClosedFooter;
          plan.reason = ScanReason::ClosedFooter;
          plan.closed_segment = true;
          plan.valid_prefix = static_cast<std::uint64_t>(data.size());
          plan.torn_suffix = 0;
          plan.alert_al14 = false;
          return plan;
        }
      }
      return torn_at(ScanReason::IncompleteHeader, plan.records, plan.last_sequence, offset,
                     data.size());
    }
    v1::RecordHeader header{};
    if (!v1::decode_header(data.subspan(offset), header) || !magic_ok(header)) {
      return fatal_at(ScanReason::BadMagic, plan.records, plan.last_sequence, offset);
    }
    if (header.format_version != v1::kFormatVersion) {
      return fatal_at(ScanReason::BadVersion, plan.records, plan.last_sequence, offset);
    }
    if (header.payload_length > remaining - v1::kRecordHeaderSize) {
      return torn_at(ScanReason::IncompletePayload, plan.records, plan.last_sequence, offset,
                     data.size());
    }
    if (!crc_matches(data, offset, header.payload_length, header.crc32c)) {
      return fatal_at(ScanReason::CrcMismatch, plan.records, plan.last_sequence, offset);
    }
    if (have_expected && header.sequence != expected) {
      return fatal_at(ScanReason::SequenceGap, plan.records, plan.last_sequence, offset);
    }
    if (header.sequence == UINT64_MAX) {
      have_expected = true;
      expected = 0;
    } else {
      have_expected = true;
      expected = header.sequence + 1;
    }
    plan.last_sequence = header.sequence;
    ++plan.records;
    offset += v1::kRecordHeaderSize + static_cast<std::size_t>(header.payload_length);
    if (header.sequence == UINT64_MAX && offset < data.size()) {
      return fatal_at(ScanReason::SequenceGap, plan.records, plan.last_sequence, offset);
    }
  }
  return plan;
}

RecoveryResult recover_segment(const std::filesystem::path& path, const bool apply) {
  RecoveryResult result;
  std::vector<std::byte> data;
  if (!read_entire(path, data)) {
    result.action = RecoveryApply::Refused;
    result.plan.kind = ScanKind::Fatal;
    result.plan.reason = ScanReason::Ok;
    return result;
  }
  result.plan = plan_segment(data);
  if (result.plan.kind == ScanKind::Ok || result.plan.closed_segment) {
    result.action = RecoveryApply::None;
    return result;
  }
  if (result.plan.kind != ScanKind::TornTail) {
    result.action = RecoveryApply::Refused;
    return result;
  }
  const auto prefix = std::span<const std::byte>(data.data(), static_cast<std::size_t>(result.plan.valid_prefix));
  const RecoveryPlan verified = plan_segment(prefix);
  if (verified.kind != ScanKind::Ok) {
    result.action = RecoveryApply::Refused;
    result.plan.kind = ScanKind::Fatal;
    result.plan.alert_al14 = false;
    return result;
  }
  if (!apply) {
    result.action = RecoveryApply::DryRun;
    return result;
  }

  const auto backup = backup_path_for(path);
  const bool backup_exists = ::access(backup.c_str(), F_OK) == 0;
  if (backup_exists) {
    if (!same_bytes(backup, data)) {
      result.action = RecoveryApply::Refused;
      result.plan.kind = ScanKind::Fatal;
      result.plan.alert_al14 = true;
      return result;
    }
  } else if (!write_durable(backup, data) || !same_bytes(backup, data)) {
    result.action = RecoveryApply::Refused;
    result.plan.kind = ScanKind::Fatal;
    result.plan.alert_al14 = true;
    return result;
  }

  const auto temp = temp_path_for(path);
  if (!write_durable(temp, prefix)) {
    ::unlink(temp.c_str());
    result.action = RecoveryApply::Refused;
    result.plan.kind = ScanKind::Fatal;
    result.plan.alert_al14 = true;
    return result;
  }
  std::vector<std::byte> temp_bytes;
  const RecoveryPlan temp_plan = [&] {
    if (!read_entire(temp, temp_bytes)) {
      return fatal_at(ScanReason::Ok, 0, 0, 0);
    }
    return plan_segment(temp_bytes);
  }();
  const bool temp_matches =
      temp_bytes.size() == prefix.size() &&
      (prefix.empty() || std::memcmp(temp_bytes.data(), prefix.data(), prefix.size()) == 0);
  if (!temp_matches || temp_plan.kind != ScanKind::Ok) {
    ::unlink(temp.c_str());
    result.action = RecoveryApply::Refused;
    result.plan.kind = ScanKind::Fatal;
    result.plan.alert_al14 = true;
    return result;
  }
  if (::rename(temp.c_str(), path.c_str()) != 0) {
    ::unlink(temp.c_str());
    result.action = RecoveryApply::Refused;
    result.plan.kind = ScanKind::Fatal;
    result.plan.alert_al14 = true;
    return result;
  }
  fsync_directory(path.parent_path());

  std::vector<std::byte> after;
  const bool after_matches =
      read_entire(path, after) && after.size() == prefix.size() &&
      (prefix.empty() || std::memcmp(after.data(), prefix.data(), prefix.size()) == 0);
  if (!after_matches || plan_segment(after).kind != ScanKind::Ok) {
    (void)write_durable(path, data);
    result.action = RecoveryApply::Refused;
    result.plan.kind = ScanKind::Fatal;
    result.plan.alert_al14 = true;
    return result;
  }
  result.action = RecoveryApply::Applied;
  result.plan.alert_al14 = true;
  result.plan.kind = ScanKind::TornTail;
  return result;
}

} // namespace trading::journal
