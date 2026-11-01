"""Event-driven backtester. The default evaluation path is walk-forward."""

from tradelab.backtest.journal_events import JournalReplayError, read_market_events
from tradelab.backtest.runner import BacktestResult, run_backtest, write_manifest
from tradelab.backtest.walk_forward import (
    WalkForwardResult,
    WindowSpec,
    evaluate,
    run_walk_forward,
)

__all__ = [
    "BacktestResult",
    "JournalReplayError",
    "WalkForwardResult",
    "WindowSpec",
    "evaluate",
    "read_market_events",
    "run_backtest",
    "run_walk_forward",
    "write_manifest",
]
