#include "trading/journal/writer.hpp"

#include "trading/journal/segment_footer.hpp"

#include <array>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace trading::journal {

namespace {

constexpr std::array<char, 4> kMagic = {'T', 'J', 'N', 'L'};

} // namespace

SegmentWriter::SegmentWriter(std::filesystem::path path) : path_(std::move(path)) {}

core::Result<void, WriterError> SegmentWriter::open() {
  if (finalized_) {
    return core::Result<void, WriterError>::err(WriterError::IoError);
  }
  if (fd_ >= 0) {
    return core::Result<void, WriterError>::ok();
  }
  const int flags = O_CREAT | O_WRONLY | O_APPEND;
  fd_ = ::open(path_.c_str(), flags, 0644);
  if (fd_ < 0) {
    return core::Result<void, WriterError>::err(WriterError::IoError);
  }
  return core::Result<void, WriterError>::ok();
}

std::vector<std::byte> encode_record(const v1::RecordType record_type,
                                     const std::span<const std::byte> payload,
                                     const std::uint64_t sequence,
                                     const std::uint64_t monotonic_ns,
                                     const std::uint64_t wall_ns) {
  v1::RecordHeader header{};
  std::memcpy(header.magic, kMagic.data(), 4);
  header.format_version = v1::kFormatVersion;
  header.record_type = static_cast<std::uint16_t>(record_type);
  header.sequence = sequence;
  header.monotonic_ns = monotonic_ns;
  header.wall_ns = wall_ns;
  const auto encoded_header = v1::encode_header(header, payload);
  std::vector<std::byte> out;
  out.reserve(encoded_header.size() + payload.size());
  out.insert(out.end(), encoded_header.begin(), encoded_header.end());
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

core::Result<std::uint64_t, WriterError> SegmentWriter::append(
    const v1::RecordType record_type, const std::span<const std::byte> payload,
    const std::uint64_t monotonic_ns, const std::uint64_t wall_ns) {
  if (fd_ < 0 || finalized_) {
    return core::Result<std::uint64_t, WriterError>::err(WriterError::NotOpen);
  }
  const std::uint64_t sequence = next_sequence_;
  const auto record = encode_record(record_type, payload, sequence, monotonic_ns, wall_ns);
  const auto* data = reinterpret_cast<const char*>(record.data());
  std::size_t written_total = 0;
  while (written_total < record.size()) {
    const ssize_t n =
        ::write(fd_, data + written_total, record.size() - written_total);
    if (n < 0) {
      return core::Result<std::uint64_t, WriterError>::err(WriterError::IoError);
    }
    written_total += static_cast<std::size_t>(n);
  }
  bytes_appended_ += record.size();
  ++next_sequence_;
  return core::Result<std::uint64_t, WriterError>::ok(sequence);
}

core::Result<void, WriterError> SegmentWriter::flush() {
  if (fd_ < 0) {
    return core::Result<void, WriterError>::err(WriterError::NotOpen);
  }
  if (::fsync(fd_) != 0) {
    return core::Result<void, WriterError>::err(WriterError::IoError);
  }
  return core::Result<void, WriterError>::ok();
}

void SegmentWriter::close() {
  if (fd_ >= 0) {
    (void)flush();
    ::close(fd_);
    fd_ = -1;
  }
}

core::Result<void, WriterError> SegmentWriter::finalize_segment(const std::uint64_t first_sequence,
                                                                const std::uint64_t record_count) {
  if (fd_ < 0 || finalized_) {
    return core::Result<void, WriterError>::err(WriterError::NotOpen);
  }
  if (record_count == 0) {
    return core::Result<void, WriterError>::err(WriterError::EncodeError);
  }
  const std::uint64_t last_sequence = next_sequence_ - 1;
  const int fd_read = ::open(path_.c_str(), O_RDONLY);
  if (fd_read < 0) {
    return core::Result<void, WriterError>::err(WriterError::IoError);
  }
  const off_t body_end = ::lseek(fd_read, 0, SEEK_END);
  ::close(fd_read);
  if (body_end < 0) {
    return core::Result<void, WriterError>::err(WriterError::IoError);
  }
  std::vector<std::byte> body(static_cast<std::size_t>(body_end));
  const int fd_body = ::open(path_.c_str(), O_RDONLY);
  if (fd_body < 0) {
    return core::Result<void, WriterError>::err(WriterError::IoError);
  }
  std::size_t read_total = 0;
  while (read_total < body.size()) {
    const ssize_t n = ::read(fd_body, reinterpret_cast<char*>(body.data()) + read_total,
                             body.size() - read_total);
    if (n <= 0) {
      ::close(fd_body);
      return core::Result<void, WriterError>::err(WriterError::IoError);
    }
    read_total += static_cast<std::size_t>(n);
  }
  ::close(fd_body);

  SegmentFooter footer{};
  footer.record_count = record_count;
  footer.last_sequence = last_sequence;
  footer.first_sequence = first_sequence;
  footer.body_crc32c = v1::compute_crc32c(body);
  const auto encoded = encode_segment_footer(footer);
  const auto* footer_data = reinterpret_cast<const char*>(encoded.data());
  std::size_t footer_written = 0;
  while (footer_written < encoded.size()) {
    const ssize_t n =
        ::write(fd_, footer_data + footer_written, encoded.size() - footer_written);
    if (n < 0) {
      return core::Result<void, WriterError>::err(WriterError::IoError);
    }
    footer_written += static_cast<std::size_t>(n);
  }
  if (::fsync(fd_) != 0) {
    return core::Result<void, WriterError>::err(WriterError::IoError);
  }
  ::close(fd_);
  fd_ = -1;
  finalized_ = true;
  return core::Result<void, WriterError>::ok();
}

} // namespace trading::journal
