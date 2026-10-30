"""In-process scripted WebSocket feed from NDJSON (no network)."""

from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator
from pathlib import Path


async def iter_scripted_ws_messages(path: Path) -> AsyncIterator[str]:
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        yield stripped
        await asyncio.sleep(0)
