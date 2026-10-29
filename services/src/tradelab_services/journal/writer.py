"""Append-only v1 journal segment writer (recording services)."""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

from tradelab_services.journal import generated as g


@dataclass
class JournalWriter:
    """Single-segment writer with explicit flush (fsync)."""

    path: Path
    next_sequence: int = 1
    _fd: int | None = None

    def open(self) -> None:
        if self._fd is not None:
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._fd = os.open(self.path, os.O_CREAT | os.O_WRONLY | os.O_APPEND, 0o644)

    def append(
        self,
        record_type: g.RecordType,
        payload: bytes,
        *,
        monotonic_ns: int,
        wall_ns: int,
    ) -> int:
        if self._fd is None:
            msg = "journal writer not open"
            raise RuntimeError(msg)
        header = g.RecordHeader(
            magic=g.JOURNAL_MAGIC.encode("ascii"),
            format_version=g.FORMAT_VERSION,
            record_type=int(record_type),
            sequence=self.next_sequence,
            monotonic_ns=monotonic_ns,
            wall_ns=wall_ns,
            payload_length=len(payload),
            crc32c=0,
        )
        record = g.encode_header(header, payload) + payload
        os.write(self._fd, record)
        seq = self.next_sequence
        self.next_sequence += 1
        return seq

    def flush(self) -> None:
        if self._fd is None:
            msg = "journal writer not open"
            raise RuntimeError(msg)
        os.fsync(self._fd)

    def close(self) -> None:
        if self._fd is None:
            return
        self.flush()
        os.close(self._fd)
        self._fd = None
