#pragma once

#include "trading/marketdata/types.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace trading::marketdata {

inline constexpr std::size_t kMaxTrackedInstruments = 1024;

// Default matches journal DQ CLI stale default until operator sets production value (Q-19).
inline constexpr std::int64_t kDefaultStaleThresholdNs = 5'000'000'000;

struct NormalizerConfig {
  std::int64_t stale_threshold_ns{kDefaultStaleThresholdNs};
};

struct ResyncPort {
  void* ctx{nullptr};
  void (*request_snapshot)(void* ctx, std::uint32_t instrument_id){nullptr};
};

struct NormalizerStats {
  std::uint64_t malformed_frames{0};
  std::uint64_t sequence_gaps{0};
  std::uint64_t resync_requests{0};
  std::uint64_t quotes_accepted{0};
  std::uint64_t trades_accepted{0};
};

struct IngestResult {
  bool accepted{false};
  bool malformed{false};
  bool sequence_gap{false};
};

// FR-MD-02–FR-MD-03, DESIGN §8.2. Parses generic frames, tracks health, blocks orders on non-HEALTHY.
class MarketDataNormalizer {
public:
  void set_config(NormalizerConfig config);
  void set_resync_port(ResyncPort port);
  void set_in_session(bool in_session) noexcept { in_session_ = in_session; }

  [[nodiscard]] IngestResult ingest(std::span<const std::byte> frame, std::int64_t receive_mono_ns,
                                    std::int64_t wall_ns) noexcept;

  void apply_snapshot(std::uint32_t instrument_id, std::uint64_t snapshot_sequence) noexcept;
  void mark_down(std::uint32_t instrument_id) noexcept;
  void tick(std::int64_t now_mono_ns) noexcept;

  [[nodiscard]] InstrumentHealth health(std::uint32_t instrument_id) const noexcept;
  [[nodiscard]] bool orders_allowed(std::uint32_t instrument_id) const noexcept;
  [[nodiscard]] const NormalizerStats& stats() const noexcept { return stats_; }

private:
  struct Track {
    InstrumentHealth health{InstrumentHealth::Down};
    bool seen{false};
    std::uint64_t next_expected_sequence{1};
    std::int64_t last_receive_mono_ns{0};
  };

  [[nodiscard]] Track* track_for(std::uint32_t instrument_id) noexcept;
  [[nodiscard]] const Track* track_for(std::uint32_t instrument_id) const noexcept;
  bool accept_sequenced_update(Track& track, std::uint32_t instrument_id,
                               std::uint64_t sequence) noexcept;

  NormalizerConfig config_{};
  ResyncPort resync_{};
  NormalizerStats stats_{};
  std::array<Track, kMaxTrackedInstruments> tracks_{};
  bool in_session_{true};
  std::int64_t last_wall_ns_{0};
};

} // namespace trading::marketdata
