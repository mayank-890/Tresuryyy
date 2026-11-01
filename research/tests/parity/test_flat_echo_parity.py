"""Replay decisions must match C++ FlatEcho::decide on the same inputs."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path

from tradelab.backtest.flat_echo import FlatEcho, FlatParams
from tradelab.backtest.runner import Decision, run_backtest
from tradelab.backtest.types import QuoteEvent, Side, TimeInForce

REPO_ROOT = Path(__file__).resolve().parents[3]
ENGINE_BUILD = "parity-engine-build"


def _compile(binary: Path) -> None:
    sources = [
        REPO_ROOT / "research" / "tests" / "parity" / "flat_echo_decide.cpp",
        REPO_ROOT / "engine" / "src" / "strategy" / "strategies" / "flat_echo" / "strategy.cpp",
    ]
    command = [
        "g++",
        "-std=c++20",
        "-fsanitize=address,undefined",
        f"-I{REPO_ROOT / 'engine' / 'include'}",
        f"-I{REPO_ROOT / 'engine' / 'src' / 'strategy'}",
        *[str(path) for path in sources],
        "-o",
        str(binary),
    ]
    subprocess.run(command, check=True, capture_output=True, text=True)


def _cpp_decides(binary: Path, script: str) -> list[tuple[int, ...]]:
    completed = subprocess.run(
        [str(binary)],
        input=script,
        check=True,
        capture_output=True,
        text=True,
    )
    rows: list[tuple[int, ...]] = []
    expect = 0
    for line in completed.stdout.splitlines():
        parts = line.split()
        if parts[0] == "COUNT":
            expect = int(parts[1])
            continue
        values = tuple(int(part) for part in parts)
        assert len(values) == 8
        rows.append(values)
        expect -= 1
    assert expect == 0
    return rows


def _intent_row(decision: Decision) -> tuple[int, ...]:
    intent = decision.intent
    return (
        intent.strategy_id,
        intent.instrument_id,
        int(intent.side),
        intent.qty_lots,
        intent.price_ticks,
        int(intent.time_in_force),
        intent.param_version,
        intent.snapshot_sequence,
    )


def test_replay_decisions_match_cpp_flat_echo() -> None:
    events = [
        QuoteEvent(200, 20, 2, 42, 10, 12, 1, 1),
        QuoteEvent(100, 10, 1, 99, 1, 2, 1, 1),
        QuoteEvent(300, 30, 3, 42, 11, 14, 1, 1),
    ]
    params = FlatParams(
        version=4,
        instrument_id=42,
        qty_lots=2,
        side=Side.BUY,
        time_in_force=TimeInForce.IMMEDIATE_OR_CANCEL,
    )
    first = run_backtest(
        FlatEcho(7),
        params,
        events,
        latency_ns=0,
        slippage_ticks=0,
        engine_build_hash=ENGINE_BUILD,
    )
    second = run_backtest(
        FlatEcho(7),
        params,
        list(reversed(events)),
        latency_ns=0,
        slippage_ticks=0,
        engine_build_hash=ENGINE_BUILD,
    )
    assert first.decisions == second.decisions
    assert first.config_hash == second.config_hash
    assert first.data_snapshot_id == second.data_snapshot_id
    assert first.engine_build_hash == ENGINE_BUILD

    # Snapshots follow the runner: latest quote per instrument, id order.
    script = "\n".join(
        [
            "DECIDE 7 4 42 2 1 2 1 1",
            "99 1 2 1 1 100",
            "DECIDE 7 4 42 2 1 2 2 2",
            "42 10 12 1 1 200",
            "99 1 2 1 1 100",
            "DECIDE 7 4 42 2 1 2 3 2",
            "42 11 14 1 1 300",
            "99 1 2 1 1 100",
            "",
        ]
    )
    with tempfile.TemporaryDirectory() as temp:
        binary = Path(temp) / "flat_echo_decide"
        _compile(binary)
        cpp_rows = _cpp_decides(binary, script)

    python_rows = [_intent_row(decision) for decision in first.decisions]
    assert python_rows == cpp_rows
    assert python_rows[0] == (7, 42, 1, 2, 12, 2, 4, 2)
    assert python_rows[1] == (7, 42, 1, 2, 14, 2, 4, 3)
