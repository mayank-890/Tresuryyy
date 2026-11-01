"""Spread-crossing fill with fixed integer slippage. No fees or queue model."""

from __future__ import annotations

from tradelab.backtest.types import OrderIntent, Quote, Side

FILL_MODEL_NAME = "spread_cross_fixed_slippage"
FILL_MODEL_VERSION = 1


def cross_fill_price(intent: OrderIntent, quote: Quote, slippage_ticks: int) -> int | None:
    """Return the fill price in ticks, or None when the order does not cross.

    A buy crosses when ask <= limit. The fill is ask + slippage_ticks.
    A sell crosses when bid >= limit. The fill is bid - slippage_ticks.
    Slippage is a fixed number of ticks supplied by the run, not a market estimate.
    """

    if type(slippage_ticks) is not int or slippage_ticks < 0:
        raise ValueError("slippage_ticks")
    if quote.instrument_id != intent.instrument_id or intent.qty_lots <= 0:
        return None
    if intent.side is Side.BUY and quote.ask_ticks <= intent.price_ticks:
        return quote.ask_ticks + slippage_ticks
    if intent.side is Side.SELL and quote.bid_ticks >= intent.price_ticks:
        return quote.bid_ticks - slippage_ticks
    return None
