"""Feed health tracking for recording (gaps and staleness)."""

from __future__ import annotations

from dataclasses import dataclass

REASON_SEQUENCE_GAP = 1
REASON_STALE = 2
REASON_DISCONNECTED = 3


@dataclass
class FeedHealthTracker:
    instrument_id: int
    last_sequence: int | None = None
    last_update_mono_ns: int = 0
    stale_after_ns: int = 5_000_000_000

    def observe_sequence(self, sequence: int) -> int | None:
        """Return REASON_SEQUENCE_GAP when a gap is detected."""
        if self.last_sequence is not None and sequence > self.last_sequence + 1:
            self.last_sequence = sequence
            return REASON_SEQUENCE_GAP
        self.last_sequence = sequence
        return None

    def touch(self, mono_ns: int) -> None:
        self.last_update_mono_ns = mono_ns

    def stale_reason(self, mono_ns: int) -> int | None:
        if self.last_update_mono_ns == 0:
            return None
        if mono_ns - self.last_update_mono_ns > self.stale_after_ns:
            return REASON_STALE
        return None
