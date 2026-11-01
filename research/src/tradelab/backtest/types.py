"""Market and intent types aligned with trading::strategy::IStrategy."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from typing import Protocol, TypeVar


class Side(IntEnum):
    BUY = 1
    SELL = 2


class TimeInForce(IntEnum):
    DAY = 1
    IMMEDIATE_OR_CANCEL = 2
    FILL_OR_KILL = 3


@dataclass(frozen=True, slots=True)
class Quote:
    instrument_id: int
    bid_ticks: int
    ask_ticks: int
    bid_size_lots: int
    ask_size_lots: int
    exchange_time_ns: int


@dataclass(frozen=True, slots=True)
class MarketSnapshot:
    """Immutable quote view for one decision. Quotes are owned by the caller."""

    sequence: int
    quotes: tuple[Quote, ...]


@dataclass(frozen=True, slots=True)
class OrderIntent:
    """Strategy output. The engine assigns a client order id later."""

    strategy_id: int
    instrument_id: int
    side: Side
    qty_lots: int
    price_ticks: int
    time_in_force: TimeInForce
    param_version: int
    snapshot_sequence: int


class Params(Protocol):
    version: int

    def canonical(self) -> dict[str, int]:
        """Stable integer fields included in the config hash."""


P = TypeVar("P", bound=Params, contravariant=True)


class Strategy(Protocol[P]):
    """Same decide contract as IStrategy: intents only, no broker or journal."""

    name: str

    def decide(self, market: MarketSnapshot, params: P) -> list[OrderIntent]:
        """Return zero or more intents. Must not touch a broker, journal, or socket."""


@dataclass(frozen=True, slots=True)
class QuoteEvent:
    exchange_time_ns: int
    receive_time_ns: int
    sequence: int
    instrument_id: int
    bid_ticks: int
    ask_ticks: int
    bid_size_lots: int
    ask_size_lots: int


@dataclass(frozen=True, slots=True)
class TradeEvent:
    exchange_time_ns: int
    receive_time_ns: int
    sequence: int
    instrument_id: int
    price_ticks: int
    size_lots: int


ReplayEvent = QuoteEvent | TradeEvent


def event_sort_key(event: ReplayEvent) -> tuple[int, int, int]:
    """DESIGN §14 order: exchange time, receive time, sequence."""

    return (event.exchange_time_ns, event.receive_time_ns, event.sequence)
