"""Event-driven backtest runner. Strategies emit intents only."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

from tradelab.backtest.fillmodel.spread import (
    FILL_MODEL_NAME,
    FILL_MODEL_VERSION,
    cross_fill_price,
)
from tradelab.backtest.types import (
    MarketSnapshot,
    OrderIntent,
    P,
    Params,
    Quote,
    QuoteEvent,
    ReplayEvent,
    Strategy,
    TimeInForce,
    event_sort_key,
)


@dataclass(frozen=True, slots=True)
class Decision:
    exchange_time_ns: int
    receive_time_ns: int
    sequence: int
    intent: OrderIntent


@dataclass(frozen=True, slots=True)
class Fill:
    instrument_id: int
    side: int
    qty_lots: int
    price_ticks: int
    decision_sequence: int
    fill_sequence: int
    param_version: int
    snapshot_sequence: int


@dataclass(frozen=True, slots=True)
class Unfilled:
    intent: OrderIntent
    reason: str


@dataclass(frozen=True, slots=True)
class BacktestResult:
    config_hash: str
    data_snapshot_id: str
    engine_build_hash: str
    decisions: tuple[Decision, ...]
    fills: tuple[Fill, ...]
    unfilled: tuple[Unfilled, ...]
    param_version: int
    first_exchange_time_ns: int | None
    last_exchange_time_ns: int | None

    def manifest(self) -> dict[str, object]:
        return {
            "config_hash": self.config_hash,
            "data_snapshot_id": self.data_snapshot_id,
            "engine_build_hash": self.engine_build_hash,
            "param_version": self.param_version,
            "fill_model": FILL_MODEL_NAME,
            "fill_model_version": FILL_MODEL_VERSION,
            "first_exchange_time_ns": self.first_exchange_time_ns,
            "last_exchange_time_ns": self.last_exchange_time_ns,
            "decisions": [_decision_dict(item) for item in self.decisions],
            "fills": [_fill_dict(item) for item in self.fills],
            "unfilled": [_unfilled_dict(item) for item in self.unfilled],
        }


@dataclass(slots=True)
class _Resting:
    intent: OrderIntent
    eligible_receive_ns: int
    decision_sequence: int


def run_backtest(
    strategy: Strategy[P],
    params: P,
    events: list[ReplayEvent] | tuple[ReplayEvent, ...],
    *,
    latency_ns: int,
    slippage_ticks: int,
    engine_build_hash: str,
) -> BacktestResult:
    """Replay events in time order and record intents and fills.

    An order can fill only on a quote whose receive time is at or after
    the decision receive time plus latency_ns. Strategies are not given a
    broker, a journal writer, or a socket.
    """

    latency = _non_negative_int("latency_ns", latency_ns)
    slippage = _non_negative_int("slippage_ticks", slippage_ticks)
    build_hash = _build_hash(engine_build_hash)
    ordered = tuple(sorted(events, key=event_sort_key))
    _reject_bad_events(ordered)

    book: dict[int, Quote] = {}
    resting: list[_Resting] = []
    decisions: list[Decision] = []
    fills: list[Fill] = []
    unfilled: list[Unfilled] = []

    for event in ordered:
        if not isinstance(event, QuoteEvent):
            continue
        book[event.instrument_id] = Quote(
            instrument_id=event.instrument_id,
            bid_ticks=event.bid_ticks,
            ask_ticks=event.ask_ticks,
            bid_size_lots=event.bid_size_lots,
            ask_size_lots=event.ask_size_lots,
            exchange_time_ns=event.exchange_time_ns,
        )
        _fill_resting(
            resting,
            book,
            active_instrument=event.instrument_id,
            receive_time_ns=event.receive_time_ns,
            fill_sequence=event.sequence,
            slippage_ticks=slippage,
            fills=fills,
            unfilled=unfilled,
        )
        snapshot = MarketSnapshot(
            sequence=event.sequence,
            quotes=tuple(book[key] for key in sorted(book)),
        )
        for intent in strategy.decide(snapshot, params):
            decisions.append(
                Decision(
                    exchange_time_ns=event.exchange_time_ns,
                    receive_time_ns=event.receive_time_ns,
                    sequence=event.sequence,
                    intent=intent,
                )
            )
            if intent.qty_lots <= 0:
                continue
            resting.append(
                _Resting(
                    intent=intent,
                    eligible_receive_ns=event.receive_time_ns + latency,
                    decision_sequence=event.sequence,
                )
            )
        _fill_resting(
            resting,
            book,
            active_instrument=event.instrument_id,
            receive_time_ns=event.receive_time_ns,
            fill_sequence=event.sequence,
            slippage_ticks=slippage,
            fills=fills,
            unfilled=unfilled,
        )

    for order in resting:
        unfilled.append(Unfilled(order.intent, "expired"))

    exchange_times = [event.exchange_time_ns for event in ordered]
    return BacktestResult(
        config_hash=config_hash(strategy.name, params, latency, slippage),
        data_snapshot_id=data_snapshot_id(ordered),
        engine_build_hash=build_hash,
        decisions=tuple(decisions),
        fills=tuple(fills),
        unfilled=tuple(unfilled),
        param_version=params.version,
        first_exchange_time_ns=exchange_times[0] if exchange_times else None,
        last_exchange_time_ns=exchange_times[-1] if exchange_times else None,
    )


def config_hash(strategy_name: str, params: Params, latency_ns: int, slippage_ticks: int) -> str:
    document = {
        "fill_model": FILL_MODEL_NAME,
        "fill_model_version": FILL_MODEL_VERSION,
        "latency_ns": latency_ns,
        "params": params.canonical(),
        "param_version": params.version,
        "slippage_ticks": slippage_ticks,
        "strategy": strategy_name,
    }
    encoded = json.dumps(document, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def data_snapshot_id(events: tuple[ReplayEvent, ...]) -> str:
    lines = [_canonical_event(event) for event in events]
    return hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest()


def write_manifest(result: BacktestResult, root: Path) -> Path:
    """Write research/results-style manifest: <root>/<config_hash>/manifest.json."""

    destination = root / result.config_hash
    destination.mkdir(parents=True, exist_ok=True)
    path = destination / "manifest.json"
    payload = json.dumps(result.manifest(), indent=2, sort_keys=True) + "\n"
    path.write_text(payload, encoding="utf-8")
    return path


def _fill_resting(
    resting: list[_Resting],
    book: dict[int, Quote],
    *,
    active_instrument: int,
    receive_time_ns: int,
    fill_sequence: int,
    slippage_ticks: int,
    fills: list[Fill],
    unfilled: list[Unfilled],
) -> None:
    still: list[_Resting] = []
    for order in resting:
        if order.eligible_receive_ns > receive_time_ns:
            still.append(order)
            continue
        if order.intent.instrument_id != active_instrument:
            still.append(order)
            continue
        quote = book[active_instrument]
        price = cross_fill_price(order.intent, quote, slippage_ticks)
        if price is not None:
            fills.append(
                Fill(
                    instrument_id=order.intent.instrument_id,
                    side=int(order.intent.side),
                    qty_lots=order.intent.qty_lots,
                    price_ticks=price,
                    decision_sequence=order.decision_sequence,
                    fill_sequence=fill_sequence,
                    param_version=order.intent.param_version,
                    snapshot_sequence=order.intent.snapshot_sequence,
                )
            )
            continue
        if order.intent.time_in_force in (
            TimeInForce.IMMEDIATE_OR_CANCEL,
            TimeInForce.FILL_OR_KILL,
        ):
            unfilled.append(Unfilled(order.intent, "cancelled"))
            continue
        still.append(order)
    resting[:] = still


def _canonical_event(event: ReplayEvent) -> str:
    if isinstance(event, QuoteEvent):
        return (
            "q,"
            f"{event.exchange_time_ns},{event.receive_time_ns},{event.sequence},"
            f"{event.instrument_id},{event.bid_ticks},{event.ask_ticks},"
            f"{event.bid_size_lots},{event.ask_size_lots}"
        )
    return (
        "t,"
        f"{event.exchange_time_ns},{event.receive_time_ns},{event.sequence},"
        f"{event.instrument_id},{event.price_ticks},{event.size_lots}"
    )


def _decision_dict(item: Decision) -> dict[str, object]:
    intent = item.intent
    return {
        "exchange_time_ns": item.exchange_time_ns,
        "receive_time_ns": item.receive_time_ns,
        "sequence": item.sequence,
        "strategy_id": intent.strategy_id,
        "instrument_id": intent.instrument_id,
        "side": int(intent.side),
        "qty_lots": intent.qty_lots,
        "price_ticks": intent.price_ticks,
        "time_in_force": int(intent.time_in_force),
        "param_version": intent.param_version,
        "snapshot_sequence": intent.snapshot_sequence,
    }


def _fill_dict(item: Fill) -> dict[str, int]:
    return {
        "instrument_id": item.instrument_id,
        "side": item.side,
        "qty_lots": item.qty_lots,
        "price_ticks": item.price_ticks,
        "decision_sequence": item.decision_sequence,
        "fill_sequence": item.fill_sequence,
        "param_version": item.param_version,
        "snapshot_sequence": item.snapshot_sequence,
    }


def _unfilled_dict(item: Unfilled) -> dict[str, object]:
    return {
        "reason": item.reason,
        "strategy_id": item.intent.strategy_id,
        "instrument_id": item.intent.instrument_id,
        "side": int(item.intent.side),
        "qty_lots": item.intent.qty_lots,
        "price_ticks": item.intent.price_ticks,
        "snapshot_sequence": item.intent.snapshot_sequence,
    }


def _non_negative_int(name: str, value: int) -> int:
    if type(value) is not int or value < 0:
        raise ValueError(name)
    return value


def _build_hash(value: str) -> str:
    if not isinstance(value, str) or value.strip() == "":
        raise ValueError("engine_build_hash")
    return value


def _reject_bad_events(events: tuple[ReplayEvent, ...]) -> None:
    for event in events:
        _non_negative_int("sequence", event.sequence)
        _non_negative_int("exchange_time_ns", event.exchange_time_ns)
        _non_negative_int("receive_time_ns", event.receive_time_ns)
