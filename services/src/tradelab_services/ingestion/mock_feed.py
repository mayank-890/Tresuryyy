"""Deterministic mock public feed (NDJSON fixtures)."""

from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path


def iter_mock_messages(path: Path) -> Iterator[str]:
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped:
            yield stripped
