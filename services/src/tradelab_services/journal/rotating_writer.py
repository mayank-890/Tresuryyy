"""Rotating v1 journal segments with TJNF footers (T-0105 / T-0106)."""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

from tradelab_services.journal import generated as g
from tradelab_services.journal.segment_footer import SegmentFooter, crc32c, encode_footer
from tradelab_services.journal.writer import JournalWriter

DEFAULT_MAX_SEGMENT_BYTES = 256 * 1024 * 1024


@dataclass(frozen=True, slots=True)
class RotationPolicy:
    max_segment_bytes: int = DEFAULT_MAX_SEGMENT_BYTES
    max_segment_age_ns: int = 0


class RotatingJournalWriter:
    """Append-only writer that rotates segments on size or age."""

    def __init__(self, journal_root: Path, policy: RotationPolicy | None = None) -> None:
        self.journal_root = journal_root
        self.policy = policy or RotationPolicy()
        self.next_sequence = 1
        self._writer: JournalWriter | None = None
        self._segment_first_sequence = 0
        self._segment_record_count = 0
        self._segment_bytes = 0
        self._segment_opened_wall_ns = 0
        self._open = False

    @property
    def active_path(self) -> Path | None:
        return self._writer.path if self._writer else None

    def open(self) -> None:
        if self._open:
            return
        self.journal_root.mkdir(parents=True, exist_ok=True)
        self._open_new_segment()
        self._open = True

    def _segment_path(self, first_sequence: int) -> Path:
        return self.journal_root / f"{first_sequence:020d}.journal"

    def _open_new_segment(self) -> None:
        self._segment_first_sequence = self.next_sequence
        self._segment_record_count = 0
        self._segment_bytes = 0
        self._segment_opened_wall_ns = 0
        path = self._segment_path(self._segment_first_sequence)
        self._writer = JournalWriter(path, next_sequence=self.next_sequence)
        self._writer.open()

    def _should_rotate(self, wall_ns: int) -> bool:
        if self._segment_record_count == 0:
            return False
        if self.policy.max_segment_bytes > 0 and self._segment_bytes >= self.policy.max_segment_bytes:
            return True
        if (
            self.policy.max_segment_age_ns > 0
            and self._segment_opened_wall_ns > 0
            and wall_ns - self._segment_opened_wall_ns >= self.policy.max_segment_age_ns
        ):
            return True
        return False

    def append(
        self,
        record_type: g.RecordType,
        payload: bytes,
        *,
        monotonic_ns: int,
        wall_ns: int,
    ) -> int:
        if not self._open or self._writer is None:
            msg = "rotating journal writer not open"
            raise RuntimeError(msg)
        if self._segment_record_count == 0:
            self._segment_opened_wall_ns = wall_ns
        seq = self._writer.append(
            record_type,
            payload,
            monotonic_ns=monotonic_ns,
            wall_ns=wall_ns,
        )
        self._segment_record_count += 1
        self._segment_bytes = self._writer.bytes_written
        self.next_sequence = self._writer.next_sequence
        if self._should_rotate(wall_ns):
            self.rotate()
        return seq

    def flush(self) -> None:
        if self._writer is not None:
            self._writer.flush()

    def rotate(self) -> None:
        self._finalize_active()
        self._open_new_segment()

    def _finalize_active(self) -> None:
        if self._writer is None:
            return
        if self._segment_record_count == 0:
            self._writer.close()
            self._writer = None
            return
        path = self._writer.path
        last_sequence = self._writer.next_sequence - 1
        record_count = self._segment_record_count
        self._writer.flush()
        fd = self._writer._fd
        if fd is None:
            msg = "journal segment not open for finalize"
            raise RuntimeError(msg)
        body = path.read_bytes()
        footer = SegmentFooter(
            record_count=record_count,
            last_sequence=last_sequence,
            body_crc32c=crc32c(body),
        )
        os.write(fd, encode_footer(footer))
        os.fsync(fd)
        os.close(fd)
        self._writer._fd = None
        self._writer = None

    def shutdown(self) -> None:
        if not self._open:
            return
        if self._writer is not None and self._segment_record_count > 0:
            self._finalize_active()
        elif self._writer is not None:
            self._writer.close()
            self._writer = None
        self._open = False
