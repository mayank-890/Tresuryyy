"""CLI entry: recording-only tick capture to the journal."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from tradelab_services.ingestion.recorder import RecorderConfig, default_journal_path, run_recording_session


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Record Delta public market data to the v1 journal (no trading)."
    )
    parser.add_argument(
        "--mode",
        choices=("mock",),
        default="mock",
        help="Feed source (mock fixture NDJSON; live WS/REST TODO(operator))",
    )
    parser.add_argument(
        "--instruments",
        type=Path,
        required=True,
        help="Recording instrument TOML (instrument_id, symbol, tick_size, lot_size)",
    )
    parser.add_argument(
        "--mock-feed",
        type=Path,
        help="NDJSON fixture for mock mode",
    )
    parser.add_argument(
        "--journal",
        type=Path,
        default=None,
        help="Output journal segment path (default: TRADELAB_JOURNAL_ROOT)",
    )
    parser.add_argument(
        "--profile",
        default="recording",
        help="Profile name stored in SessionStart",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.mode == "mock" and args.mock_feed is None:
        print("--mock-feed is required for mock mode", file=sys.stderr)
        return 2

    journal_path = args.journal if args.journal is not None else default_journal_path()
    config = RecorderConfig(
        profile_name=args.profile,
        journal_path=journal_path,
        instruments_file=args.instruments,
        mock_feed_file=args.mock_feed,
    )
    try:
        rc = run_recording_session(config)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    if rc == 0:
        print(journal_path)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
