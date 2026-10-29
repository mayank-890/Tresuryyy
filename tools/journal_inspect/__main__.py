"""CLI entry: python3 tools/journal_inspect/__main__.py <path> [options]."""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

_INSPECT_DIR = Path(__file__).resolve().parent
if str(_INSPECT_DIR) not in sys.path:
    sys.path.insert(0, str(_INSPECT_DIR))

from segment import (
    InspectStatus,
    filter_records,
    format_record,
    inspect_segment,
    parse_record_type,
)


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate and pretty-print v1 journal segment files (read-only)."
    )
    parser.add_argument("path", type=Path, help="Journal segment file")
    parser.add_argument(
        "--type",
        dest="record_type",
        metavar="NAME",
        help="Filter by record type name (e.g. SessionStart)",
    )
    parser.add_argument("--seq-min", type=int, default=None, help="Minimum sequence (inclusive)")
    parser.add_argument("--seq-max", type=int, default=None, help="Maximum sequence (inclusive)")
    parser.add_argument(
        "--digest",
        action="store_true",
        help="Print SHA-256 of file before scan (read-only verification aid)",
    )
    args = parser.parse_args(argv)

    path = args.path if args.path.is_absolute() else _repo_root() / args.path
    if not path.is_file():
        print(f"error: not a file: {path}", file=sys.stderr)
        return InspectStatus.FATAL

    data = path.read_bytes()
    if args.digest:
        digest = hashlib.sha256(data).hexdigest()
        print(f"sha256={digest} bytes={len(data)}")

    report = inspect_segment(data)
    print(f"status={report.status.name} {report.message}")
    if report.alert_code:
        print(f"alert={report.alert_code}")

    type_filter: int | None = None
    if args.record_type is not None:
        try:
            type_filter = parse_record_type(args.record_type)
        except ValueError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return InspectStatus.FATAL

    if report.status is not InspectStatus.FATAL:
        visible = filter_records(
            report.records,
            record_type=type_filter,
            seq_min=args.seq_min,
            seq_max=args.seq_max,
        )
        for rec in visible:
            print(format_record(rec))

    if report.error_offset is not None and report.status is InspectStatus.FATAL:
        print(f"fatal_offset={report.error_offset}", file=sys.stderr)

    return int(report.status)


if __name__ == "__main__":
    raise SystemExit(main())
