"""Read market events from a journal v1 segment. Does not modify the bytes."""

from __future__ import annotations

import struct

from tradelab.backtest.types import QuoteEvent, ReplayEvent, TradeEvent
from tradelab.journal import generated as journal

_FOOTER_MAGIC = b"TJNF"


class JournalReplayError(ValueError):
    """The segment is not safe to replay (bad magic, CRC, or sequence gap)."""


def read_market_events(data: bytes) -> tuple[tuple[ReplayEvent, ...], bool]:
    """Return market events and whether a short non-footer tail was ignored.

    A CRC failure, bad magic, or sequence gap raises JournalReplayError.
    A valid TJNF footer ends the scan. Records other than quotes and trades
    are skipped. Quote and trade exchange times come from the payload.
    Receive time is the header monotonic timestamp.
    """

    events: list[ReplayEvent] = []
    offset = 0
    expected: int | None = None
    torn_tail = False

    while offset < len(data):
        remaining = data[offset:]
        if remaining.startswith(_FOOTER_MAGIC):
            break
        if len(remaining) < journal.RECORD_HEADER_SIZE:
            torn_tail = True
            break
        header = journal.decode_header(remaining)
        if header.magic != b"TJNL" or header.format_version != journal.FORMAT_VERSION:
            raise JournalReplayError("bad journal header")
        record_end = offset + journal.RECORD_HEADER_SIZE + header.payload_length
        if record_end > len(data):
            torn_tail = True
            break
        payload = data[offset + journal.RECORD_HEADER_SIZE : record_end]
        if not _crc_ok(header, payload):
            raise JournalReplayError("journal crc mismatch")
        if expected is None:
            expected = header.sequence
        elif header.sequence != expected:
            raise JournalReplayError("journal sequence gap")
        expected = header.sequence + 1
        event = _market_event(header, payload)
        if event is not None:
            events.append(event)
        offset = record_end

    return tuple(events), torn_tail


def _crc_ok(header: journal.RecordHeader, payload: bytes) -> bool:
    encoded = journal.encode_header(
        journal.RecordHeader(
            magic=b"TJNL",
            format_version=header.format_version,
            record_type=header.record_type,
            sequence=header.sequence,
            monotonic_ns=header.monotonic_ns,
            wall_ns=header.wall_ns,
            payload_length=len(payload),
            crc32c=0,
        ),
        payload,
    )
    (expected,) = struct.unpack_from("<I", encoded, 36)
    return bool(header.crc32c == expected)


def _market_event(header: journal.RecordHeader, payload: bytes) -> ReplayEvent | None:
    if header.record_type == int(journal.RecordType.MarketQuote):
        quote = journal.decode_market_quote(payload)
        return QuoteEvent(
            exchange_time_ns=quote.exchange_time_ns,
            receive_time_ns=header.monotonic_ns,
            sequence=header.sequence,
            instrument_id=quote.instrument_id,
            bid_ticks=quote.bid_ticks,
            ask_ticks=quote.ask_ticks,
            bid_size_lots=quote.bid_size_lots,
            ask_size_lots=quote.ask_size_lots,
        )
    if header.record_type == int(journal.RecordType.MarketTrade):
        trade = journal.decode_market_trade(payload)
        return TradeEvent(
            exchange_time_ns=trade.exchange_time_ns,
            receive_time_ns=header.monotonic_ns,
            sequence=header.sequence,
            instrument_id=trade.instrument_id,
            price_ticks=trade.price_ticks,
            size_lots=trade.size_lots,
        )
    return None


def encode_quote_record(
    *,
    sequence: int,
    monotonic_ns: int,
    wall_ns: int,
    instrument_id: int,
    bid_ticks: int,
    ask_ticks: int,
    bid_size_lots: int,
    ask_size_lots: int,
    exchange_time_ns: int,
) -> bytes:
    """Build one valid quote record for tests and fixtures."""

    payload = journal.encode_market_quote(
        journal.MarketQuotePayload(
            instrument_id=instrument_id,
            bid_ticks=bid_ticks,
            ask_ticks=ask_ticks,
            bid_size_lots=bid_size_lots,
            ask_size_lots=ask_size_lots,
            exchange_time_ns=exchange_time_ns,
        )
    )
    header = journal.encode_header(
        journal.RecordHeader(
            magic=b"TJNL",
            format_version=journal.FORMAT_VERSION,
            record_type=int(journal.RecordType.MarketQuote),
            sequence=sequence,
            monotonic_ns=monotonic_ns,
            wall_ns=wall_ns,
            payload_length=len(payload),
            crc32c=0,
        ),
        payload,
    )
    return header + payload
