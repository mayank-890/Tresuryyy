"""Reconnect backoff bounds."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "services" / "src"))

from tradelab_services.ingestion.backoff import reconnect_delay_s  # noqa: E402


def test_backoff_within_cap() -> None:
    for attempt in range(10):
        delay = reconnect_delay_s(attempt)
        assert 0.0 <= delay <= 72.0
