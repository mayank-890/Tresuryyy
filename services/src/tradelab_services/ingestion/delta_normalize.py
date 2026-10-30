"""Normalize Delta public feed messages into journal payloads."""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any

from tradelab_services.ingestion.instruments import RecordingInstrument
from tradelab_services.journal import generated as g


@dataclass(frozen=True, slots=True)
class NormalizedQuote:
    payload: g.MarketQuotePayload


@dataclass(frozen=True, slots=True)
class NormalizedTrade:
    payload: g.MarketTradePayload


def _price_to_ticks(price: str, tick_size: int) -> int:
    if tick_size <= 0:
        return 0
    return int(round(float(price) / tick_size))


def _size_to_lots(size: str, lot_size: int) -> int:
    if lot_size <= 0:
        return 0
    return int(round(float(size) / lot_size))


def parse_public_message(raw: str, instrument: RecordingInstrument) -> NormalizedQuote | NormalizedTrade | None:
    try:
        doc: dict[str, Any] = json.loads(raw)
    except json.JSONDecodeError:
        return None

    msg_type = doc.get("type")
    if doc.get("symbol") != instrument.symbol:
        return None

    if msg_type == "quote":
        exchange_time_ns = int(doc.get("timestamp_us", 0)) * 1000
        payload = g.MarketQuotePayload(
            instrument_id=instrument.instrument_id,
            bid_ticks=_price_to_ticks(str(doc.get("bid", "0")), instrument.tick_size),
            ask_ticks=_price_to_ticks(str(doc.get("ask", "0")), instrument.tick_size),
            bid_size_lots=_size_to_lots(str(doc.get("bid_size", "0")), instrument.lot_size),
            ask_size_lots=_size_to_lots(str(doc.get("ask_size", "0")), instrument.lot_size),
            exchange_time_ns=exchange_time_ns,
        )
        return NormalizedQuote(payload=payload)

    if msg_type == "trade":
        side_raw = str(doc.get("side", "buy")).lower()
        side = g.Side.Buy if side_raw == "buy" else g.Side.Sell
        exchange_time_ns = int(doc.get("timestamp_us", 0)) * 1000
        payload = g.MarketTradePayload(
            instrument_id=instrument.instrument_id,
            price_ticks=_price_to_ticks(str(doc.get("price", "0")), instrument.tick_size),
            size_lots=_size_to_lots(str(doc.get("size", "0")), instrument.lot_size),
            side=side,
            exchange_time_ns=exchange_time_ns,
        )
        return NormalizedTrade(payload=payload)

    if msg_type == "v2_ticker":
        quotes = doc.get("quotes", {})
        exchange_time_ns = int(doc.get("timestamp", 0)) * 1000
        payload = g.MarketQuotePayload(
            instrument_id=instrument.instrument_id,
            bid_ticks=_price_to_ticks(str(quotes.get("best_bid", "0")), instrument.tick_size),
            ask_ticks=_price_to_ticks(str(quotes.get("best_ask", "0")), instrument.tick_size),
            bid_size_lots=_size_to_lots(str(quotes.get("bid_size", "0")), instrument.lot_size),
            ask_size_lots=_size_to_lots(str(quotes.get("ask_size", "0")), instrument.lot_size),
            exchange_time_ns=exchange_time_ns,
        )
        return NormalizedQuote(payload=payload)

    return None
