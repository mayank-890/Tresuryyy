"""Walk-forward evaluation. This is the default research evaluation path."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

from tradelab.backtest.runner import (
    BacktestResult,
    config_hash,
    data_snapshot_id,
    run_backtest,
)
from tradelab.backtest.types import P, Params, ReplayEvent, Strategy, event_sort_key


@dataclass(frozen=True, slots=True)
class WindowSpec:
    """Event counts. The test window begins where the train window ends."""

    train_size: int
    test_size: int
    step: int


@dataclass(frozen=True, slots=True)
class EventWindow:
    index: int
    train_start: int
    train_end: int
    test_start: int
    test_end: int


@dataclass(frozen=True, slots=True)
class WindowRun:
    index: int
    train_start: int
    train_end: int
    test_start: int
    test_end: int
    train: BacktestResult
    test: BacktestResult


@dataclass(frozen=True, slots=True)
class WalkForwardResult:
    config_hash: str
    data_snapshot_id: str
    engine_build_hash: str
    train_size: int
    test_size: int
    step: int
    windows: tuple[WindowRun, ...]
    test_decision_count: int
    test_fill_count: int
    test_unfilled_count: int
    train_decision_count: int
    train_fill_count: int

    def manifest(self) -> dict[str, object]:
        return {
            "config_hash": self.config_hash,
            "data_snapshot_id": self.data_snapshot_id,
            "engine_build_hash": self.engine_build_hash,
            "train_size": self.train_size,
            "test_size": self.test_size,
            "step": self.step,
            "test_decision_count": self.test_decision_count,
            "test_fill_count": self.test_fill_count,
            "test_unfilled_count": self.test_unfilled_count,
            "train_decision_count": self.train_decision_count,
            "train_fill_count": self.train_fill_count,
            "windows": [_window_dict(window) for window in self.windows],
        }


def plan_windows(event_count: int, spec: WindowSpec) -> tuple[EventWindow, ...]:
    """Sequential train/test slices. The next window starts `step` events later."""

    train_size = _positive_int("train_size", spec.train_size)
    test_size = _positive_int("test_size", spec.test_size)
    step = _positive_int("step", spec.step)
    if type(event_count) is not int or event_count < 0:
        raise ValueError("event_count")
    windows: list[EventWindow] = []
    start = 0
    while start + train_size + test_size <= event_count:
        train_end = start + train_size
        test_end = train_end + test_size
        windows.append(
            EventWindow(
                index=len(windows),
                train_start=start,
                train_end=train_end,
                test_start=train_end,
                test_end=test_end,
            )
        )
        start += step
    return tuple(windows)


def run_walk_forward(
    strategy: Strategy[P],
    params: P,
    events: list[ReplayEvent] | tuple[ReplayEvent, ...],
    spec: WindowSpec,
    *,
    latency_ns: int,
    slippage_ticks: int,
    engine_build_hash: str,
) -> WalkForwardResult:
    """Run the backtester on each train slice and the following test slice.

    Aggregated decision and fill counts are the test windows only.
    Each slice keeps its own config hash, data snapshot id, and engine build hash.
    """

    ordered = tuple(sorted(events, key=event_sort_key))
    _require_events(ordered)
    probe = run_backtest(
        strategy,
        params,
        (),
        latency_ns=latency_ns,
        slippage_ticks=slippage_ticks,
        engine_build_hash=engine_build_hash,
    )
    windows = plan_windows(len(ordered), spec)
    runs: list[WindowRun] = []
    for window in windows:
        train_events = ordered[window.train_start : window.train_end]
        test_events = ordered[window.test_start : window.test_end]
        runs.append(
            WindowRun(
                index=window.index,
                train_start=window.train_start,
                train_end=window.train_end,
                test_start=window.test_start,
                test_end=window.test_end,
                train=_run_slice(
                    strategy,
                    params,
                    train_events,
                    latency_ns=latency_ns,
                    slippage_ticks=slippage_ticks,
                    engine_build_hash=engine_build_hash,
                ),
                test=_run_slice(
                    strategy,
                    params,
                    test_events,
                    latency_ns=latency_ns,
                    slippage_ticks=slippage_ticks,
                    engine_build_hash=engine_build_hash,
                ),
            )
        )
    finished = tuple(runs)
    return WalkForwardResult(
        config_hash=_walk_config_hash(strategy.name, params, latency_ns, slippage_ticks, spec),
        data_snapshot_id=data_snapshot_id(ordered),
        engine_build_hash=probe.engine_build_hash,
        train_size=spec.train_size,
        test_size=spec.test_size,
        step=spec.step,
        windows=finished,
        test_decision_count=sum(len(item.test.decisions) for item in finished),
        test_fill_count=sum(len(item.test.fills) for item in finished),
        test_unfilled_count=sum(len(item.test.unfilled) for item in finished),
        train_decision_count=sum(len(item.train.decisions) for item in finished),
        train_fill_count=sum(len(item.train.fills) for item in finished),
    )


def evaluate(
    strategy: Strategy[P],
    params: P,
    events: list[ReplayEvent] | tuple[ReplayEvent, ...],
    spec: WindowSpec,
    *,
    latency_ns: int,
    slippage_ticks: int,
    engine_build_hash: str,
) -> WalkForwardResult:
    """Default evaluation path. Walk-forward, not one in-sample run."""

    return run_walk_forward(
        strategy,
        params,
        events,
        spec,
        latency_ns=latency_ns,
        slippage_ticks=slippage_ticks,
        engine_build_hash=engine_build_hash,
    )


def write_walk_forward_manifest(result: WalkForwardResult, root: Path) -> Path:
    destination = root / result.config_hash
    destination.mkdir(parents=True, exist_ok=True)
    path = destination / "manifest.json"
    payload = json.dumps(result.manifest(), indent=2, sort_keys=True) + "\n"
    path.write_text(payload, encoding="utf-8")
    return path


def _run_slice(
    strategy: Strategy[P],
    params: P,
    events: tuple[ReplayEvent, ...],
    *,
    latency_ns: int,
    slippage_ticks: int,
    engine_build_hash: str,
) -> BacktestResult:
    return run_backtest(
        strategy,
        params,
        events,
        latency_ns=latency_ns,
        slippage_ticks=slippage_ticks,
        engine_build_hash=engine_build_hash,
    )


def _walk_config_hash(
    strategy_name: str,
    params: Params,
    latency_ns: int,
    slippage_ticks: int,
    spec: WindowSpec,
) -> str:
    document = {
        "base_config_hash": config_hash(strategy_name, params, latency_ns, slippage_ticks),
        "evaluation": "walk_forward",
        "step": spec.step,
        "test_size": spec.test_size,
        "train_size": spec.train_size,
    }
    encoded = json.dumps(document, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _window_dict(window: WindowRun) -> dict[str, object]:
    return {
        "index": window.index,
        "train_start": window.train_start,
        "train_end": window.train_end,
        "test_start": window.test_start,
        "test_end": window.test_end,
        "train_config_hash": window.train.config_hash,
        "train_data_snapshot_id": window.train.data_snapshot_id,
        "train_engine_build_hash": window.train.engine_build_hash,
        "test_config_hash": window.test.config_hash,
        "test_data_snapshot_id": window.test.data_snapshot_id,
        "test_engine_build_hash": window.test.engine_build_hash,
        "test_decision_count": len(window.test.decisions),
        "test_fill_count": len(window.test.fills),
    }


def _positive_int(name: str, value: int) -> int:
    if type(value) is not int or value <= 0:
        raise ValueError(name)
    return value


def _require_events(events: tuple[ReplayEvent, ...]) -> None:
    for event in events:
        _non_negative(event.sequence, "sequence")
        _non_negative(event.exchange_time_ns, "exchange_time_ns")
        _non_negative(event.receive_time_ns, "receive_time_ns")


def _non_negative(value: int, name: str) -> None:
    if type(value) is not int or value < 0:
        raise ValueError(name)
