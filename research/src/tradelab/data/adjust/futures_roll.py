"""Futures roll adjustment stub (Phase 2+).

Continuous futures backtests require roll rules and adjustment factors at contract
boundaries. T-0101 records raw venue candles only; adjustment is not applied here.

TODO(operator): define roll calendar and adjustment method (back-adjust vs ratio)
when Indian commodities and multi-expiry crypto futures are in scope.
"""

from __future__ import annotations

from tradelab.data.ingest.delta_rest import OhlcvBar


def apply_futures_roll_adjustment(bars: list[OhlcvBar]) -> list[OhlcvBar]:
    """Pass-through stub until roll logic is specified (DESIGN / Phase 2)."""
    return list(bars)
