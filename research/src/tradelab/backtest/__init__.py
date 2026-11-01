"""Event-driven backtester. Strategies emit intents only."""

from tradelab.backtest.journal_events import JournalReplayError, read_market_events
from tradelab.backtest.runner import BacktestResult, run_backtest, write_manifest

__all__ = [
    "BacktestResult",
    "JournalReplayError",
    "read_market_events",
    "run_backtest",
    "write_manifest",
]
