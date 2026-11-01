"""SAMPLE — NOT FOR TRADING. Same decision rule as the C++ flat_echo strategy."""

from __future__ import annotations

from dataclasses import dataclass

from tradelab.backtest.types import (
    MarketSnapshot,
    OrderIntent,
    Quote,
    Side,
    TimeInForce,
)


@dataclass(frozen=True, slots=True)
class FlatParams:
    version: int
    instrument_id: int
    qty_lots: int
    side: Side
    time_in_force: TimeInForce

    def canonical(self) -> dict[str, int]:
        return {
            "instrument_id": self.instrument_id,
            "qty_lots": self.qty_lots,
            "side": int(self.side),
            "time_in_force": int(self.time_in_force),
            "version": self.version,
        }


def size_lots(params: FlatParams) -> int:
    """Pure. Non-positive quantities emit no lots. Not a risk limit."""

    if params.qty_lots <= 0:
        return 0
    return params.qty_lots


class FlatEcho:
    """At most one intent: buy at the ask, sell at the bid."""

    name = "flat_echo"

    def __init__(self, strategy_id: int) -> None:
        if type(strategy_id) is not int:
            raise TypeError("strategy_id")
        self.strategy_id = strategy_id

    def decide(self, market: MarketSnapshot, params: FlatParams) -> list[OrderIntent]:
        if params.version < 0:
            return []
        lots = size_lots(params)
        if lots <= 0:
            return []
        if params.side not in (Side.BUY, Side.SELL):
            return []
        quote = _first_quote(market, params.instrument_id)
        if quote is None:
            return []
        price = quote.ask_ticks if params.side is Side.BUY else quote.bid_ticks
        return [
            OrderIntent(
                strategy_id=self.strategy_id,
                instrument_id=params.instrument_id,
                side=params.side,
                qty_lots=lots,
                price_ticks=price,
                time_in_force=params.time_in_force,
                param_version=params.version,
                snapshot_sequence=market.sequence,
            )
        ]


def _first_quote(market: MarketSnapshot, instrument_id: int) -> Quote | None:
    for quote in market.quotes:
        if quote.instrument_id == instrument_id:
            return quote
    return None
