"""Instrument table for recording (no order routing)."""

from __future__ import annotations

from dataclasses import dataclass
import tomllib
from pathlib import Path


@dataclass(frozen=True, slots=True)
class RecordingInstrument:
    instrument_id: int
    symbol: str
    tick_size: int
    lot_size: int


def load_recording_instruments(path: Path) -> list[RecordingInstrument]:
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    rows = data.get("instrument")
    if not isinstance(rows, list):
        msg = "expected [[instrument]] table"
        raise ValueError(msg)
    out: list[RecordingInstrument] = []
    for row in rows:
        if not isinstance(row, dict):
            msg = "invalid instrument row"
            raise ValueError(msg)
        out.append(
            RecordingInstrument(
                instrument_id=int(row["instrument_id"]),
                symbol=str(row["symbol"]),
                tick_size=int(row["tick_size"]),
                lot_size=int(row["lot_size"]),
            )
        )
    return out
