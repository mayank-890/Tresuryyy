"""CLI: journal data-quality report for v1 segments."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from journal_dq_report.analyze import build_report


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Data-quality report for a v1 journal segment.")
    parser.add_argument("segment", type=Path, help="Path to journal segment file")
    parser.add_argument(
        "--stale-threshold-ms",
        type=float,
        default=5000.0,
        help="Gap between market events (same instrument) treated as stale (default 5000 ms)",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    path = args.segment
    if not path.is_file():
        print(f"not a file: {path}", file=sys.stderr)
        return 2

    stale_threshold_ns = int(args.stale_threshold_ms * 1_000_000)
    report = build_report(path, path.read_bytes(), stale_threshold_ns=stale_threshold_ns)
    if args.format == "json":
        sys.stdout.write(report.to_json())
    else:
        sys.stdout.write(report.to_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
