"""Normalized OHLCV column names and Arrow schema (T-0101)."""

from __future__ import annotations

import pyarrow as pa

VENUE_DELTA_PUBLIC = "delta_public"
SOURCE_DELTA_REST = "delta_rest"

OHLCV_COLUMNS: tuple[str, ...] = (
    "symbol",
    "venue",
    "resolution",
    "open_time_ns",
    "open",
    "high",
    "low",
    "close",
    "volume",
    "source",
)

OHLCV_ARROW_SCHEMA = pa.schema(
    [
        ("symbol", pa.string()),
        ("venue", pa.string()),
        ("resolution", pa.string()),
        ("open_time_ns", pa.int64()),
        ("open", pa.float64()),
        ("high", pa.float64()),
        ("low", pa.float64()),
        ("close", pa.float64()),
        ("volume", pa.float64()),
        ("source", pa.string()),
    ]
)
