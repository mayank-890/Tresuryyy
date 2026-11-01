"""Walk-forward windowing and aggregation on a small quote fixture."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from tradelab.backtest.flat_echo import FlatEcho, FlatParams
from tradelab.backtest.runner import run_backtest
from tradelab.backtest.types import QuoteEvent, Side, TimeInForce
from tradelab.backtest.walk_forward import (
    WindowSpec,
    evaluate,
    plan_windows,
    run_walk_forward,
    write_walk_forward_manifest,
)

ENGINE = "test-engine-build"


def _params() -> FlatParams:
    return FlatParams(
        version=1,
        instrument_id=7,
        qty_lots=1,
        side=Side.BUY,
        time_in_force=TimeInForce.DAY,
    )


def _fixture(count: int) -> list[QuoteEvent]:
    return [
        QuoteEvent(
            exchange_time_ns=index * 10,
            receive_time_ns=index * 10,
            sequence=index + 1,
            instrument_id=7,
            bid_ticks=10 + index,
            ask_ticks=12 + index,
            bid_size_lots=1,
            ask_size_lots=1,
        )
        for index in range(count)
    ]


def test_plan_windows_steps_through_the_series() -> None:
    spec = WindowSpec(train_size=4, test_size=2, step=2)
    windows = plan_windows(10, spec)
    bounds = [
        (item.train_start, item.train_end, item.test_start, item.test_end) for item in windows
    ]
    assert bounds == [
        (0, 4, 4, 6),
        (2, 6, 6, 8),
        (4, 8, 8, 10),
    ]
    assert plan_windows(5, spec) == ()
    overlap = plan_windows(4, WindowSpec(train_size=2, test_size=1, step=1))
    assert len(overlap) == 2
    assert overlap[1].train_start == 1
    assert overlap[1].test_end == 4


def test_walk_forward_aggregates_test_windows_only() -> None:
    events = _fixture(10)
    spec = WindowSpec(train_size=4, test_size=2, step=2)
    kwargs = {"latency_ns": 0, "slippage_ticks": 1, "engine_build_hash": ENGINE}
    result = evaluate(FlatEcho(1), _params(), list(reversed(events)), spec, **kwargs)
    again = run_walk_forward(FlatEcho(1), _params(), events, spec, **kwargs)
    assert result == again
    assert len(result.windows) == 3
    assert result.windows[0].test.first_exchange_time_ns == 40
    assert result.windows[0].test.last_exchange_time_ns == 50
    assert result.windows[2].test_start == 8
    assert result.test_decision_count == 6
    assert result.test_fill_count == 6
    assert result.train_decision_count == 12
    whole = run_backtest(FlatEcho(1), _params(), events, **kwargs)
    assert len(whole.decisions) == 10
    assert result.test_decision_count != len(whole.decisions)


def test_each_slice_records_hashes(tmp_path: Path) -> None:
    events = _fixture(10)
    spec = WindowSpec(train_size=4, test_size=2, step=2)
    result = evaluate(
        FlatEcho(1),
        _params(),
        events,
        spec,
        latency_ns=0,
        slippage_ticks=0,
        engine_build_hash=ENGINE,
    )
    assert result.engine_build_hash == ENGINE
    assert result.config_hash
    assert result.data_snapshot_id
    snapshot_ids = {window.test.data_snapshot_id for window in result.windows}
    assert len(snapshot_ids) == 3
    for window in result.windows:
        assert window.test.engine_build_hash == ENGINE
        assert window.train.engine_build_hash == ENGINE
        assert window.test.config_hash == window.train.config_hash
        assert window.test.data_snapshot_id != window.train.data_snapshot_id
        assert window.test.config_hash != result.config_hash
    other = evaluate(
        FlatEcho(1),
        _params(),
        events,
        WindowSpec(train_size=4, test_size=2, step=4),
        latency_ns=0,
        slippage_ticks=0,
        engine_build_hash=ENGINE,
    )
    assert other.config_hash != result.config_hash
    assert other.data_snapshot_id == result.data_snapshot_id
    path = write_walk_forward_manifest(result, tmp_path)
    manifest = json.loads(path.read_text(encoding="utf-8"))
    assert manifest["config_hash"] == result.config_hash
    assert manifest["data_snapshot_id"] == result.data_snapshot_id
    assert manifest["engine_build_hash"] == ENGINE
    assert manifest["windows"][0]["test_engine_build_hash"] == ENGINE
    assert manifest["test_decision_count"] == 6


def test_rejects_non_positive_window_spec() -> None:
    events = _fixture(3)
    with pytest.raises(ValueError, match="step"):
        plan_windows(3, WindowSpec(train_size=1, test_size=1, step=0))
    with pytest.raises(ValueError, match="train_size"):
        evaluate(
            FlatEcho(1),
            _params(),
            events,
            WindowSpec(train_size=0, test_size=1, step=1),
            latency_ns=0,
            slippage_ticks=0,
            engine_build_hash=ENGINE,
        )
