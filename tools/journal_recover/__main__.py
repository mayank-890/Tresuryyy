"""CLI: python3 -m journal_recover <segment> [--apply]."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

_RECOVER_DIR = Path(__file__).resolve().parent
if str(_RECOVER_DIR) not in sys.path:
    sys.path.insert(0, str(_RECOVER_DIR))

from recover import apply_recovery, exit_code, format_report  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Detect a torn v1 journal tail (AL-14) and, with --apply, "
            "truncate to the last valid prefix. Recording path only."
        )
    )
    parser.add_argument("path", type=Path, help="Journal segment file")
    parser.add_argument(
        "--apply",
        action="store_true",
        help=(
            "Copy the original bytes to <segment>.torn, then replace the "
            "segment with the inspector-verified prefix. Without this flag "
            "the file is not modified."
        ),
    )
    args = parser.parse_args(argv)

    path = args.path if args.path.is_absolute() else Path.cwd() / args.path
    if not path.is_file():
        print(f"error: not a file: {path}", file=sys.stderr)
        return 2

    report = apply_recovery(path, apply=args.apply)
    sys.stdout.write(format_report(report))
    return exit_code(report)


if __name__ == "__main__":
    raise SystemExit(main())
