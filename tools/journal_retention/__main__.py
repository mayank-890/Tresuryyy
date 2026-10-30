"""CLI: journal retention status (Python mirror for ops)."""

from __future__ import annotations

import argparse
import os
from pathlib import Path

from journal_retention.footer import decode_footer, FOOTER_SIZE


def _scan(root: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for path in sorted(root.glob("*.journal")):
        size = path.stat().st_size
        closed = False
        record_count = 0
        last_sequence = 0
        if size >= FOOTER_SIZE:
            tail = path.read_bytes()[-FOOTER_SIZE:]
            footer = decode_footer(tail)
            if footer is not None:
                closed = True
                record_count = footer.record_count
                last_sequence = footer.last_sequence
        first_sequence = int(path.stem) if path.stem.isdigit() else 0
        rows.append(
            {
                "path": path.name,
                "closed": closed,
                "first_sequence": first_sequence,
                "record_count": record_count,
                "last_sequence": last_sequence,
                "size_bytes": size,
            }
        )
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description="Journal retention status (T-0105)")
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(os.environ.get("TRADELAB_JOURNAL_ROOT", "")),
        help="Journal root directory",
    )
    args = parser.parse_args()
    if not args.root:
        print("TRADELAB_JOURNAL_ROOT or --root is required", flush=True)
        return 2
    rows = _scan(args.root)
    print(f"root={args.root} segments={len(rows)}")
    for row in rows:
        state = "closed" if row["closed"] else "open"
        print(
            f"{state} first_seq={row['first_sequence']} records={row['record_count']} "
            f"last_seq={row['last_sequence']} bytes={row['size_bytes']} path={row['path']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
