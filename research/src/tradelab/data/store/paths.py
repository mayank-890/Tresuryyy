"""External data root layout for OHLCV Parquet (REPO_STRUCTURE §4)."""

from __future__ import annotations

import os
from pathlib import Path


def resolve_data_root(explicit: Path | None = None) -> Path:
    """Return the operator-configured external data root."""
    if explicit is not None:
        return explicit.expanduser().resolve()
    raw = os.environ.get("TRADELAB_DATA_ROOT")
    if not raw:
        msg = (
            "TRADELAB_DATA_ROOT is not set. "
            "TODO(operator): configure the host path for market-data archives."
        )
        raise ValueError(msg)
    return Path(raw).expanduser().resolve()


def ohlcv_parquet_path(
    data_root: Path,
    *,
    symbol: str,
    resolution: str,
    venue: str = "delta",
) -> Path:
    """Hive-style path: ``{root}/ohlcv/{venue}/symbol={sym}/resolution={res}/bars.parquet``."""
    safe_symbol = symbol.replace("/", "_")
    return (
        data_root
        / "ohlcv"
        / venue
        / f"symbol={safe_symbol}"
        / f"resolution={resolution}"
        / "bars.parquet"
    )
