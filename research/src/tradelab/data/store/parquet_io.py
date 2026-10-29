"""Parquet read/write for normalized OHLCV (ADR-0015)."""

from __future__ import annotations

from pathlib import Path

import pyarrow as pa
import pyarrow.parquet as pq

from tradelab.data.ingest.delta_rest import OhlcvBar
from tradelab.data.schema import OHLCV_ARROW_SCHEMA, OHLCV_COLUMNS


def bars_to_table(bars: list[OhlcvBar]) -> pa.Table:
    if not bars:
        return pa.table({col: [] for col in OHLCV_COLUMNS}, schema=OHLCV_ARROW_SCHEMA)
    columns: dict[str, list[object]] = {col: [] for col in OHLCV_COLUMNS}
    for bar in bars:
        columns["symbol"].append(bar.symbol)
        columns["venue"].append(bar.venue)
        columns["resolution"].append(bar.resolution)
        columns["open_time_ns"].append(bar.open_time_ns)
        columns["open"].append(bar.open)
        columns["high"].append(bar.high)
        columns["low"].append(bar.low)
        columns["close"].append(bar.close)
        columns["volume"].append(bar.volume)
        columns["source"].append(bar.source)
    return pa.table(columns, schema=OHLCV_ARROW_SCHEMA)


def table_to_bars(table: pa.Table) -> list[OhlcvBar]:
    rows: list[OhlcvBar] = []
    for idx in range(table.num_rows):
        rows.append(
            OhlcvBar(
                symbol=str(table.column("symbol")[idx].as_py()),
                venue=str(table.column("venue")[idx].as_py()),
                resolution=str(table.column("resolution")[idx].as_py()),
                open_time_ns=int(table.column("open_time_ns")[idx].as_py()),
                open=float(table.column("open")[idx].as_py()),
                high=float(table.column("high")[idx].as_py()),
                low=float(table.column("low")[idx].as_py()),
                close=float(table.column("close")[idx].as_py()),
                volume=float(table.column("volume")[idx].as_py()),
                source=str(table.column("source")[idx].as_py()),
            )
        )
    return rows


def write_bars_parquet(path: Path, bars: list[OhlcvBar]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    table = bars_to_table(bars)
    pq.write_table(table, path, compression="zstd")


def read_bars_parquet(path: Path) -> list[OhlcvBar]:
    table = pq.read_table(path)
    return table_to_bars(table)


def merge_write_bars_parquet(path: Path, new_bars: list[OhlcvBar]) -> list[OhlcvBar]:
    """Merge with existing file on ``open_time_ns``; return combined sorted bars."""
    existing: list[OhlcvBar] = []
    if path.is_file():
        existing = read_bars_parquet(path)
    merged: dict[int, OhlcvBar] = {b.open_time_ns: b for b in existing}
    for bar in new_bars:
        merged[bar.open_time_ns] = bar
    combined = [merged[k] for k in sorted(merged)]
    write_bars_parquet(path, combined)
    return combined
