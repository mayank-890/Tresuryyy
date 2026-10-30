"""CLI entry: recording-only tick capture to the journal."""

from __future__ import annotations

import argparse
import asyncio
import sys
from pathlib import Path

from tradelab_services.ingestion.delta_ws import DEFAULT_DELTA_PUBLIC_WS_URL
from tradelab_services.ingestion.recorder import (
    RecorderConfig,
    default_journal_root,
    run_live_ws_session,
    run_mock_ws_session,
    run_recording_session,
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Record Delta public market data to the v1 journal (no trading)."
    )
    parser.add_argument(
        "--mode",
        choices=("mock", "mock-ws", "live"),
        default="mock",
        help="Feed source: NDJSON fixture, scripted WS frames, or live Delta public WS",
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
        help="NDJSON fixture for mock / mock-ws modes",
    )
    parser.add_argument(
        "--journal-root",
        type=Path,
        default=None,
        help="Rotating journal directory (default: TRADELAB_JOURNAL_ROOT session dir)",
    )
    parser.add_argument(
        "--profile",
        default="recording",
        help="Profile name stored in SessionStart",
    )
    parser.add_argument(
        "--ws-url",
        default=DEFAULT_DELTA_PUBLIC_WS_URL,
        help="Delta public WebSocket URL (live mode only)",
    )
    parser.add_argument(
        "--max-segment-bytes",
        type=int,
        default=256 * 1024 * 1024,
        help="Rotate journal segment at this size (bytes)",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.mode in ("mock", "mock-ws") and args.mock_feed is None:
        print("--mock-feed is required for mock and mock-ws modes", file=sys.stderr)
        return 2

    journal_root = args.journal_root if args.journal_root is not None else default_journal_root()
    config = RecorderConfig(
        profile_name=args.profile,
        journal_root=journal_root,
        instruments_file=args.instruments,
        mock_feed_file=args.mock_feed,
        ws_url=args.ws_url,
        max_segment_bytes=args.max_segment_bytes,
    )

    try:
        if args.mode == "mock":
            rc = run_recording_session(config)
        elif args.mode == "mock-ws":
            rc = asyncio.run(run_mock_ws_session(config))
        else:
            rc = asyncio.run(run_live_ws_session(config))
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if rc == 2:
        print(
            "refused: empty instrument list — no live connect (TODO(operator) Q-16)",
            file=sys.stderr,
        )
        return rc
    if rc == 0:
        print(journal_root)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
