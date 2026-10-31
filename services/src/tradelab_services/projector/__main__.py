"""CLI: python -m tradelab_services.projector --segment <file> [--dry-run]."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from tradelab_services.projector.project import exit_code, format_report, project_segment


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Project a v1 journal segment into PostgreSQL. "
            "The journal file is not modified. A database outage does not change recording."
        )
    )
    parser.add_argument("--segment", type=Path, required=True, help="Journal segment file")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Parse and report only. Do not open a database connection.",
    )
    parser.add_argument(
        "--database-url",
        default=None,
        help="Postgres URL. Defaults to DATABASE_URL. Not printed.",
    )
    args = parser.parse_args(argv)
    path = args.segment if args.segment.is_absolute() else Path.cwd() / args.segment
    if not path.is_file():
        print(f"error: not a file: {path}", file=sys.stderr)
        return 2
    report = project_segment(path, apply=not args.dry_run, database_url=args.database_url)
    sys.stdout.write(format_report(report))
    return exit_code(report)


if __name__ == "__main__":
    raise SystemExit(main())
