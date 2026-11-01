"""Event-driven backtester: ordering, fills, latency, and result hashes."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from tradelab.backtest.fillmodel.spread import cross_fill_price
from tradelab.backtest.flat_echo import FlatEcho, FlatParams, size_lots
from tradelab.backtest.journal_events import (
    JournalReplayError,
    encode_quote_record,
    read_market_events,
)
from tradelab.backtest.runner import run_backtest, write_manifest
from tradelab.backtest.types import (
    MarketSnapshot,
    OrderIntent,
    Quote,
    QuoteEvent,
    Side,
    TimeInForce,
    TradeEvent,
)

ENGINE = "test-engine-build"


def _params(**overrides: int | Side | TimeInForce) -> FlatParams:
    values: dict[str, int | Side | TimeInForce] = {
        "version": 4,
        "instrument_id": 42,
        "qty_lots": 2,
        "side": Side.BUY,
        "time_in_force": TimeInForce.DAY,
    }
    values.update(overrides)
    return FlatParams(
        version=int(values["version"]),
        instrument_id=int(values["instrument_id"]),
        qty_lots=int(values["qty_lots"]),
        side=values["side"] if isinstance(values["side"], Side) else Side(int(values["side"])),
        time_in_force=(
            values["time_in_force"]
            if isinstance(values["time_in_force"], TimeInForce)
            else TimeInForce(int(values["time_in_force"]))
        ),
    )


def _quote(
    sequence: int,
    exchange_time_ns: int,
    receive_time_ns: int,
    bid: int,
    ask: int,
    instrument_id: int = 42,
) -> QuoteEvent:
    return QuoteEvent(
        exchange_time_ns=exchange_time_ns,
        receive_time_ns=receive_time_ns,
        sequence=sequence,
        instrument_id=instrument_id,
        bid_ticks=bid,
        ask_ticks=ask,
        bid_size_lots=1,
        ask_size_lots=1,
    )


class BidBelow:
    name = "bid_below"

    def __init__(self, gap: int, time_in_force: TimeInForce) -> None:
        self.gap = gap
        self.time_in_force = time_in_force

    def decide(self, market: MarketSnapshot, params: FlatParams) -> list[OrderIntent]:
        quote = market.quotes[-1]
        return [
            OrderIntent(
                strategy_id=3,
                instrument_id=quote.instrument_id,
                side=Side.BUY,
                qty_lots=1,
                price_ticks=quote.ask_ticks - self.gap,
                time_in_force=self.time_in_force,
                param_version=params.version,
                snapshot_sequence=market.sequence,
            )
        ]


def test_flat_echo_matches_documented_cpp_vector() -> None:
    market = MarketSnapshot(
        sequence=99,
        quotes=(
            Quote(99, 1, 2, 1, 1, 0),
            Quote(42, 10, 12, 1, 1, 0),
        ),
    )
    intents = FlatEcho(7).decide(market, _params(time_in_force=TimeInForce.IMMEDIATE_OR_CANCEL))
    assert len(intents) == 1
    assert intents[0].strategy_id == 7
    assert intents[0].instrument_id == 42
    assert intents[0].side is Side.BUY
    assert intents[0].qty_lots == 2
    assert intents[0].price_ticks == 12
    assert intents[0].time_in_force is TimeInForce.IMMEDIATE_OR_CANCEL
    assert intents[0].param_version == 4
    assert intents[0].snapshot_sequence == 99
    assert size_lots(_params(qty_lots=0)) == 0
    assert size_lots(_params(qty_lots=-3)) == 0


def test_same_inputs_produce_identical_results() -> None:
    events = [_quote(1, 10, 10, 10, 12), _quote(2, 20, 20, 11, 13)]
    kwargs = {
        "latency_ns": 0,
        "slippage_ticks": 1,
        "engine_build_hash": ENGINE,
    }
    first = run_backtest(FlatEcho(7), _params(), events, **kwargs)
    second = run_backtest(FlatEcho(7), _params(), list(reversed(events)), **kwargs)
    assert first.decisions == second.decisions
    assert first.fills == second.fills
    assert first.config_hash == second.config_hash
    assert first.data_snapshot_id == second.data_snapshot_id
    assert first.engine_build_hash == ENGINE
    assert first.decisions[0].intent.price_ticks == 12
    assert first.fills[0].price_ticks == 13


def test_latency_does_not_change_decisions() -> None:
    events = [_quote(1, 0, 0, 10, 12)]
    immediate = run_backtest(
        FlatEcho(7),
        _params(),
        events,
        latency_ns=0,
        slippage_ticks=1,
        engine_build_hash=ENGINE,
    )
    delayed = run_backtest(
        FlatEcho(7),
        _params(),
        events,
        latency_ns=1_000,
        slippage_ticks=1,
        engine_build_hash=ENGINE,
    )
    assert immediate.decisions == delayed.decisions
    assert len(immediate.fills) == 1
    assert immediate.fills[0].price_ticks == 13
    assert delayed.fills == ()
    assert delayed.unfilled[0].reason == "expired"
    assert immediate.config_hash != delayed.config_hash
    assert immediate.data_snapshot_id == delayed.data_snapshot_id


def test_day_order_fills_on_later_cross_and_ioc_does_not() -> None:
    events = [_quote(1, 0, 0, 10, 20), _quote(2, 50, 50, 9, 10)]
    day = run_backtest(
        BidBelow(5, TimeInForce.DAY),
        _params(),
        events,
        latency_ns=0,
        slippage_ticks=1,
        engine_build_hash=ENGINE,
    )
    # First ask 20, limit 15, no cross. Second ask 10, the resting limit 15 crosses.
    assert len(day.fills) == 1
    assert day.fills[0].price_ticks == 11
    assert day.fills[0].decision_sequence == 1
    assert day.fills[0].fill_sequence == 2

    ioc = run_backtest(
        BidBelow(5, TimeInForce.IMMEDIATE_OR_CANCEL),
        _params(),
        events,
        latency_ns=0,
        slippage_ticks=0,
        engine_build_hash=ENGINE,
    )
    assert ioc.fills == ()
    assert any(item.reason == "cancelled" for item in ioc.unfilled)


def test_trades_do_not_call_decide() -> None:
    class Counting(FlatEcho):
        def __init__(self) -> None:
            super().__init__(1)
            self.calls = 0

        def decide(self, market: MarketSnapshot, params: FlatParams) -> list[OrderIntent]:
            self.calls += 1
            return []

    strategy = Counting()
    events = [
        TradeEvent(1, 1, 1, 42, 10, 1),
        _quote(2, 2, 2, 10, 12),
    ]
    run_backtest(
        strategy,
        _params(qty_lots=0),
        events,
        latency_ns=0,
        slippage_ticks=0,
        engine_build_hash=ENGINE,
    )
    assert strategy.calls == 1


def test_sell_cross_and_no_cross() -> None:
    quote = Quote(42, 10, 12, 1, 1, 0)
    sell = OrderIntent(1, 42, Side.SELL, 1, 10, TimeInForce.DAY, 1, 1)
    assert cross_fill_price(sell, quote, 2) == 8
    quiet = OrderIntent(1, 42, Side.BUY, 1, 11, TimeInForce.DAY, 1, 1)
    assert cross_fill_price(quiet, quote, 0) is None


def test_manifest_records_three_hashes(tmp_path: Path) -> None:
    result = run_backtest(
        FlatEcho(7),
        _params(),
        [_quote(1, 5, 5, 10, 12)],
        latency_ns=0,
        slippage_ticks=0,
        engine_build_hash=ENGINE,
    )
    changed = run_backtest(
        FlatEcho(7),
        _params(),
        [_quote(1, 5, 5, 10, 13)],
        latency_ns=0,
        slippage_ticks=0,
        engine_build_hash=ENGINE,
    )
    assert result.data_snapshot_id != changed.data_snapshot_id
    assert result.config_hash == changed.config_hash
    path = write_manifest(result, tmp_path)
    manifest = json.loads(path.read_text(encoding="utf-8"))
    assert manifest["config_hash"] == result.config_hash
    assert manifest["data_snapshot_id"] == result.data_snapshot_id
    assert manifest["engine_build_hash"] == ENGINE
    assert path.parent.name == result.config_hash


def test_journal_replay_sorts_and_rejects_crc() -> None:
    later = encode_quote_record(
        sequence=1,
        monotonic_ns=10,
        wall_ns=10,
        instrument_id=42,
        bid_ticks=10,
        ask_ticks=12,
        bid_size_lots=1,
        ask_size_lots=1,
        exchange_time_ns=200,
    )
    earlier = encode_quote_record(
        sequence=2,
        monotonic_ns=20,
        wall_ns=20,
        instrument_id=42,
        bid_ticks=11,
        ask_ticks=14,
        bid_size_lots=1,
        ask_size_lots=1,
        exchange_time_ns=100,
    )
    segment = later + earlier + b"TJNF" + b"\x00" * 28
    events, torn = read_market_events(segment)
    assert torn is False
    result = run_backtest(
        FlatEcho(7),
        _params(),
        events,
        latency_ns=0,
        slippage_ticks=0,
        engine_build_hash=ENGINE,
    )
    assert [item.sequence for item in result.decisions] == [2, 1]
    assert result.decisions[0].intent.price_ticks == 14

    torn_events, torn_flag = read_market_events(later + b"\x01\x02")
    assert torn_flag is True
    assert len(torn_events) == 1

    broken = bytearray(later)
    broken[-1] ^= 0xFF
    with pytest.raises(JournalReplayError):
        read_market_events(bytes(broken))


def test_rejects_blank_build_hash_and_negative_latency() -> None:
    events = [_quote(1, 0, 0, 1, 2)]
    with pytest.raises(ValueError, match="engine_build_hash"):
        run_backtest(
            FlatEcho(7),
            _params(),
            events,
            latency_ns=0,
            slippage_ticks=0,
            engine_build_hash="  ",
        )
    with pytest.raises(ValueError, match="latency_ns"):
        run_backtest(
            FlatEcho(7),
            _params(),
            events,
            latency_ns=-1,
            slippage_ticks=0,
            engine_build_hash=ENGINE,
        )
