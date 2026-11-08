#include "trading/marketdata/normalizer.hpp"

#include "trading/marketdata/generic_frame.hpp"

namespace trading::marketdata {

namespace {

[[nodiscard]] std::uint32_t instrument_index(const std::uint32_t instrument_id) noexcept {
  return instrument_id;
}

} // namespace

void MarketDataNormalizer::set_config(const NormalizerConfig config) { config_ = config; }

void MarketDataNormalizer::set_resync_port(const ResyncPort port) { resync_ = port; }

MarketDataNormalizer::Track* MarketDataNormalizer::track_for(const std::uint32_t instrument_id) noexcept {
  const std::uint32_t index = instrument_index(instrument_id);
  if (index >= tracks_.size()) {
    return nullptr;
  }
  return &tracks_[index];
}

const MarketDataNormalizer::Track* MarketDataNormalizer::track_for(
    const std::uint32_t instrument_id) const noexcept {
  const std::uint32_t index = instrument_index(instrument_id);
  if (index >= tracks_.size()) {
    return nullptr;
  }
  return &tracks_[index];
}

bool MarketDataNormalizer::accept_sequenced_update(Track& track, const std::uint32_t instrument_id,
                                                   const std::uint64_t sequence) noexcept {
  if (track.health == InstrumentHealth::Resyncing) {
    return false;
  }

  if (!track.seen) {
    track.seen = true;
    track.next_expected_sequence = sequence + 1;
    track.health = InstrumentHealth::Healthy;
    return true;
  }

  if (sequence != track.next_expected_sequence) {
    ++stats_.sequence_gaps;
    track.health = InstrumentHealth::Resyncing;
    track.next_expected_sequence = 0;
    if (resync_.request_snapshot != nullptr) {
      resync_.request_snapshot(resync_.ctx, instrument_id);
      ++stats_.resync_requests;
    }
    return false;
  }

  track.next_expected_sequence = sequence + 1;
  track.health = InstrumentHealth::Healthy;
  return true;
}

IngestResult MarketDataNormalizer::ingest(const std::span<const std::byte> frame,
                                          const std::int64_t receive_mono_ns,
                                          const std::int64_t wall_ns) noexcept {
  (void)wall_ns;
  last_wall_ns_ = wall_ns;

  IngestResult result{};
  const ParsedFrame parsed = parse_generic_frame(frame);
  if (parsed.status != ParseStatus::Ok) {
    ++stats_.malformed_frames;
    result.malformed = true;
    return result;
  }

  Track* track = track_for(parsed.is_quote ? parsed.quote.instrument_id : parsed.trade.instrument_id);
  if (track == nullptr) {
    ++stats_.malformed_frames;
    result.malformed = true;
    return result;
  }

  const std::uint64_t sequence = parsed.is_quote ? parsed.quote.sequence : parsed.trade.sequence;
  const std::uint32_t instrument_id =
      parsed.is_quote ? parsed.quote.instrument_id : parsed.trade.instrument_id;
  if (!accept_sequenced_update(*track, instrument_id, sequence)) {
    result.sequence_gap = track->health == InstrumentHealth::Resyncing;
    return result;
  }

  track->last_receive_mono_ns = receive_mono_ns;
  result.accepted = true;
  if (parsed.is_quote) {
    ++stats_.quotes_accepted;
  } else {
    ++stats_.trades_accepted;
  }
  return result;
}

void MarketDataNormalizer::apply_snapshot(const std::uint32_t instrument_id,
                                          const std::uint64_t snapshot_sequence) noexcept {
  Track* track = track_for(instrument_id);
  if (track == nullptr) {
    return;
  }
  track->seen = true;
  track->next_expected_sequence = snapshot_sequence + 1;
  track->health = InstrumentHealth::Healthy;
}

void MarketDataNormalizer::mark_down(const std::uint32_t instrument_id) noexcept {
  Track* track = track_for(instrument_id);
  if (track == nullptr) {
    return;
  }
  track->health = InstrumentHealth::Down;
}

void MarketDataNormalizer::tick(const std::int64_t now_mono_ns) noexcept {
  if (!in_session_) {
    return;
  }
  for (Track& track : tracks_) {
    if (!track.seen || track.health == InstrumentHealth::Resyncing ||
        track.health == InstrumentHealth::Down) {
      continue;
    }
    if (track.last_receive_mono_ns == 0) {
      continue;
    }
    if ((now_mono_ns - track.last_receive_mono_ns) > config_.stale_threshold_ns) {
      track.health = InstrumentHealth::Stale;
    } else if (track.health == InstrumentHealth::Stale) {
      track.health = InstrumentHealth::Healthy;
    }
  }
}

InstrumentHealth MarketDataNormalizer::health(const std::uint32_t instrument_id) const noexcept {
  const Track* track = track_for(instrument_id);
  if (track == nullptr || !track->seen) {
    return InstrumentHealth::Down;
  }
  return track->health;
}

bool MarketDataNormalizer::orders_allowed(const std::uint32_t instrument_id) const noexcept {
  return health(instrument_id) == InstrumentHealth::Healthy;
}

} // namespace trading::marketdata
