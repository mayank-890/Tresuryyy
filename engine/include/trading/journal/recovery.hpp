#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace trading::journal {

// DESIGN §10.3. Alert raised when a torn tail is truncated.
inline constexpr std::uint16_t kAlertAl14 = 14;

enum class ScanKind : std::uint8_t { Ok = 1, TornTail = 2, Fatal = 3, ClosedFooter = 4 };

enum class ScanReason : std::uint8_t {
  Ok = 1,
  IncompleteHeader,
  IncompletePayload,
  BadMagic,
  BadVersion,
  CrcMismatch,
  SequenceGap,
  ClosedFooter,
};

// Pure scan. A torn tail is an incomplete header or payload at the end.
// A CRC mismatch or sequence gap is fatal even when it is the last record.
// A valid TJNF footer is ClosedFooter and is not a torn tail.
struct RecoveryPlan {
  ScanKind kind{ScanKind::Ok};
  ScanReason reason{ScanReason::Ok};
  std::uint64_t records{0};
  std::uint64_t last_sequence{0};
  std::uint64_t valid_prefix{0};
  std::uint64_t torn_suffix{0};
  bool alert_al14{false};
  bool closed_segment{false};
};

enum class RecoveryApply : std::uint8_t { None = 1, DryRun = 2, Applied = 3, Refused = 4 };

struct RecoveryResult {
  RecoveryPlan plan{};
  RecoveryApply action{RecoveryApply::None};
};

[[nodiscard]] RecoveryPlan plan_segment(std::span<const std::byte> data);

// Copy-safe truncate (T-0107): fsync the original bytes to `<name>.torn`,
// fsync the verified prefix, then replace the segment. Fatal scans are not
// truncated. A valid TJNF footer is left in place. `apply == false` reports
// only.
[[nodiscard]] RecoveryResult recover_segment(const std::filesystem::path& path, bool apply);

} // namespace trading::journal
