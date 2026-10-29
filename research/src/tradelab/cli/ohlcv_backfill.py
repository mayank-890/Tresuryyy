"""CLI: backfill Delta public REST OHLCV into Parquet under the external data root."""

from __future__ import annotations

import argparse
import sys
from datetime import UTC, datetime
from pathlib import Path

from tradelab.data.ingest.delta_rest import (
    DEFAULT_DELTA_REST_BASE,
    UrllibJsonClient,
    fetch_candles_range,
)
from tradelab.data.quality.checks import run_quality_checks
from tradelab.data.store.parquet_io import merge_write_bars_parquet
from tradelab.data.store.paths import ohlcv_parquet_path, resolve_data_root


def _parse_utc_date(value: str) -> datetime:
    try:
        parsed = datetime.strptime(value, "%Y-%m-%d").replace(tzinfo=UTC)
    except ValueError as exc:
        msg = f"invalid date {value!r}; use YYYY-MM-DD"
        raise argparse.ArgumentTypeError(msg) from exc
    return parsed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Backfill Delta Exchange public OHLCV candles to Parquet (no trading)."
    )
    parser.add_argument("--symbol", required=True, help="Delta symbol, e.g. BTCUSD")
    parser.add_argument("--resolution", required=True, help="Candle resolution, e.g. 1m, 1h")
    parser.add_argument("--start", required=True, type=_parse_utc_date, help="Start date (UTC)")
    parser.add_argument(
        "--end",
        required=True,
        type=_parse_utc_date,
        help="End date (UTC, exclusive)",
    )
    parser.add_argument(
        "--data-root",
        type=Path,
        default=None,
        help="External data root (default: TRADELAB_DATA_ROOT env)",
    )
    parser.add_argument(
        "--base-url",
        default=DEFAULT_DELTA_REST_BASE,
        help="Delta REST base URL (public candles only)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Fetch and report quality only; do not write Parquet",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.end <= args.start:
        print("end must be after start", file=sys.stderr)
        return 2

    start_s = int(args.start.timestamp())
    end_s = int(args.end.timestamp())

    client = UrllibJsonClient()
    bars = fetch_candles_range(
        client,
        base_url=args.base_url,
        symbol=args.symbol,
        resolution=args.resolution,
        start_s=start_s,
        end_s=end_s,
    )
    report = run_quality_checks(bars)
    print(
        f"fetched={len(bars)} gaps={report.gap_count} outliers={report.outlier_count}",
        file=sys.stderr,
    )

    if args.dry_run:
        return 0

    data_root = resolve_data_root(args.data_root)
    out_path = ohlcv_parquet_path(data_root, symbol=args.symbol, resolution=args.resolution)
    merge_write_bars_parquet(out_path, bars)
    print(str(out_path))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
