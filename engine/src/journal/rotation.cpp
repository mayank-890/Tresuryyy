#include "trading/journal/rotation.hpp"

#include "trading/journal/segment_footer.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace trading::journal {

namespace {

std::uint64_t parse_first_sequence_from_name(const std::filesystem::path& path) {
  const auto stem = path.stem().string();
  if (stem.empty() || !std::all_of(stem.begin(), stem.end(),
                                   [](const char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
    return 0;
  }
  return std::strtoull(stem.c_str(), nullptr, 10);
}

RotationError map_writer_error(const WriterError err) {
  switch (err) {
  case WriterError::EncodeError:
    return RotationError::EncodeError;
  case WriterError::NotOpen:
    return RotationError::NotOpen;
  case WriterError::IoError:
  default:
    return RotationError::IoError;
  }
}

} // namespace

std::filesystem::path segment_path_for_first_sequence(const std::filesystem::path& root,
                                                      const std::uint64_t first_sequence) {
  char name[32];
  std::snprintf(name, sizeof(name), "%020llu",
                static_cast<unsigned long long>(first_sequence));
  return root / (std::string{name} + ".journal");
}

RotatingJournalWriter::RotatingJournalWriter(std::filesystem::path journal_root,
                                             RotationPolicy rotation)
    : journal_root_(std::move(journal_root)), rotation_(rotation) {}

core::Result<void, RotationError> RotatingJournalWriter::open() {
  if (open_) {
    return core::Result<void, RotationError>::ok();
  }
  std::error_code ec;
  std::filesystem::create_directories(journal_root_, ec);
  if (ec) {
    return core::Result<void, RotationError>::err(RotationError::IoError);
  }
  const auto opened = open_new_segment();
  if (!opened.is_ok()) {
    return opened;
  }
  open_ = true;
  return core::Result<void, RotationError>::ok();
}

core::Result<void, RotationError> RotatingJournalWriter::open_new_segment() {
  segment_first_sequence_ = next_sequence_;
  segment_record_count_ = 0;
  segment_bytes_ = 0;
  segment_opened_wall_ns_ = 0;
  active_path_ = segment_path_for_first_sequence(journal_root_, segment_first_sequence_);
  writer_.emplace(active_path_);
  const auto result = writer_->open();
  if (!result.is_ok()) {
    writer_.reset();
    return core::Result<void, RotationError>::err(map_writer_error(result.error()));
  }
  return core::Result<void, RotationError>::ok();
}

core::Result<void, RotationError> RotatingJournalWriter::finalize_active() {
  if (!writer_.has_value()) {
    return core::Result<void, RotationError>::err(RotationError::NotOpen);
  }
  if (segment_record_count_ == 0) {
    writer_->close();
    writer_.reset();
    return core::Result<void, RotationError>::ok();
  }
  const auto fin = writer_->finalize_segment(segment_first_sequence_, segment_record_count_);
  if (!fin.is_ok()) {
    return core::Result<void, RotationError>::err(map_writer_error(fin.error()));
  }
  writer_.reset();
  return core::Result<void, RotationError>::ok();
}

bool RotatingJournalWriter::should_rotate(const std::uint64_t wall_ns) const {
  if (!writer_.has_value() || segment_record_count_ == 0) {
    return false;
  }
  if (rotation_.max_segment_bytes > 0 && segment_bytes_ >= rotation_.max_segment_bytes) {
    return true;
  }
  if (rotation_.max_segment_age_ns > 0 && segment_opened_wall_ns_ > 0 &&
      wall_ns > segment_opened_wall_ns_ &&
      wall_ns - segment_opened_wall_ns_ >= rotation_.max_segment_age_ns) {
    return true;
  }
  return false;
}

core::Result<std::uint64_t, RotationError> RotatingJournalWriter::append(
    const v1::RecordType record_type, const std::span<const std::byte> payload,
    const std::uint64_t monotonic_ns, const std::uint64_t wall_ns) {
  (void)monotonic_ns;
  if (!open_ || !writer_.has_value()) {
    return core::Result<std::uint64_t, RotationError>::err(RotationError::NotOpen);
  }
  if (segment_record_count_ == 0) {
    segment_opened_wall_ns_ = wall_ns;
  }
  const auto seq = writer_->append(record_type, payload, monotonic_ns, wall_ns);
  if (!seq.is_ok()) {
    return core::Result<std::uint64_t, RotationError>::err(map_writer_error(seq.error()));
  }
  ++segment_record_count_;
  segment_bytes_ = writer_->bytes_appended();
  next_sequence_ = writer_->next_sequence();
  if (should_rotate(wall_ns)) {
    const auto rotated = rotate();
    if (!rotated.is_ok()) {
      return core::Result<std::uint64_t, RotationError>::err(rotated.error());
    }
  }
  return core::Result<std::uint64_t, RotationError>::ok(seq.value());
}

core::Result<void, RotationError> RotatingJournalWriter::flush() {
  if (!writer_.has_value()) {
    return core::Result<void, RotationError>::err(RotationError::NotOpen);
  }
  const auto result = writer_->flush();
  if (!result.is_ok()) {
    return core::Result<void, RotationError>::err(map_writer_error(result.error()));
  }
  return core::Result<void, RotationError>::ok();
}

core::Result<void, RotationError> RotatingJournalWriter::rotate() {
  const auto fin = finalize_active();
  if (!fin.is_ok()) {
    return fin;
  }
  return open_new_segment();
}

void RotatingJournalWriter::shutdown() {
  if (!open_) {
    return;
  }
  if (writer_.has_value() && segment_record_count_ > 0) {
    (void)writer_->flush();
    (void)finalize_active();
  } else if (writer_.has_value()) {
    writer_->close();
    writer_.reset();
  }
  open_ = false;
}

std::vector<SegmentInfo> scan_journal_segments(const std::filesystem::path& root) {
  std::vector<SegmentInfo> out;
  if (!std::filesystem::is_directory(root)) {
    return out;
  }
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".journal") {
      continue;
    }
    SegmentInfo info{};
    info.path = entry.path();
    info.first_sequence = parse_first_sequence_from_name(info.path);
    info.size_bytes = entry.file_size();
    const auto footer = read_segment_footer(info.path);
    if (footer.is_ok()) {
      info.closed = true;
      info.record_count = footer.value().record_count;
      info.last_sequence = footer.value().last_sequence;
      if (info.first_sequence == 0) {
        info.first_sequence = footer.value().first_sequence;
      }
    }
    out.push_back(info);
  }
  std::sort(out.begin(), out.end(), [](const SegmentInfo& a, const SegmentInfo& b) {
    return a.first_sequence < b.first_sequence;
  });
  return out;
}

bool archive_verified_flag_set() {
  const char* value = std::getenv("ARCHIVE_VERIFIED");
  return value != nullptr && std::string_view{value} == "1";
}

core::Result<RetentionApplyResult, RetentionError> apply_retention(
    const std::filesystem::path& root, const RetentionPolicy& policy,
    const bool archive_verified_override) {
  const bool verified = archive_verified_override || archive_verified_flag_set();
  RetentionApplyResult result{};
  auto segments = scan_journal_segments(root);
  std::vector<SegmentInfo> closed;
  for (const auto& seg : segments) {
    if (seg.closed) {
      closed.push_back(seg);
    }
  }
  if (policy.max_closed_segments == 0 && policy.max_closed_age_ns == 0) {
    return core::Result<RetentionApplyResult, RetentionError>::ok(result);
  }

  const auto now = std::filesystem::file_time_type::clock::now();
  std::set<std::filesystem::path> to_remove;
  if (policy.max_closed_segments > 0 && closed.size() > policy.max_closed_segments) {
    const std::size_t excess = closed.size() - policy.max_closed_segments;
    for (std::size_t i = 0; i < excess; ++i) {
      to_remove.insert(closed[i].path);
    }
  }
  if (policy.max_closed_age_ns > 0) {
    for (const auto& seg : closed) {
      std::error_code ec;
      const auto mtime = std::filesystem::last_write_time(seg.path, ec);
      if (ec) {
        continue;
      }
      const auto age = now - mtime;
      const auto age_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(age).count();
      if (static_cast<std::uint64_t>(age_ns) >= policy.max_closed_age_ns) {
        to_remove.insert(seg.path);
      }
    }
  }
  result.candidates = to_remove.size();
  result.archive_verified = verified;
  if (!verified) {
    result.refused_paths.assign(to_remove.begin(), to_remove.end());
    return core::Result<RetentionApplyResult, RetentionError>::ok(result);
  }
  for (const auto& path : to_remove) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) {
      return core::Result<RetentionApplyResult, RetentionError>::err(RetentionError::IoError);
    }
    ++result.removed;
  }
  return core::Result<RetentionApplyResult, RetentionError>::ok(result);
}

} // namespace trading::journal
