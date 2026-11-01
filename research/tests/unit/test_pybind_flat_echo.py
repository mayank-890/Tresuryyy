"""Call C++ FlatEcho.decide through the pybind11 module."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

from tradelab.engine import bindings_search_root, find_bindings_module, load_native

REPO_ROOT = Path(__file__).resolve().parents[3]
SOURCE = REPO_ROOT / "engine" / "bindings" / "python" / "trading_bindings.cpp"


def _ensure_bindings() -> None:
    built = find_bindings_module()
    if built is not None and built.stat().st_mtime >= SOURCE.stat().st_mtime:
        return
    vcpkg = Path(os.environ.get("VCPKG_ROOT", REPO_ROOT / ".vcpkg"))
    toolchain = vcpkg / "scripts" / "buildsystems" / "vcpkg.cmake"
    build_dir = bindings_search_root()
    configure = [
        "cmake",
        "-S",
        str(REPO_ROOT),
        "-B",
        str(build_dir),
        "-G",
        "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        "-DTRADING_BUILD_BINDINGS=ON",
        "-DTRADING_ENABLE_GROWW=OFF",
        "-DTRADING_ENABLE_LIVE_PROFILE=OFF",
        "-DCMAKE_BUILD_TYPE=Debug",
        f"-DPython_EXECUTABLE={sys.executable}",
    ]
    env = os.environ.copy()
    homebrew = "/opt/homebrew/bin"
    path = env.get("PATH", "")
    if homebrew not in path.split(":"):
        env["PATH"] = f"{homebrew}:{path}" if path else homebrew
    subprocess.run(configure, check=True, env=env)
    subprocess.run(
        ["cmake", "--build", str(build_dir), "--target", "trading_bindings"],
        check=True,
        env=env,
    )


def test_flat_echo_decide_from_python() -> None:
    _ensure_bindings()
    native = load_native()
    strategy = native.FlatEcho(7)
    assert strategy.name() == "flat_echo"
    assert strategy.strategy_id() == 7

    params = native.FlatParams()
    params.version = 4
    params.instrument_id = 42
    params.qty_lots = 2
    params.side = native.Side.Buy
    params.time_in_force = native.TimeInForce.ImmediateOrCancel

    other = native.Quote()
    other.instrument_id = 99
    other.bid_ticks = 1
    other.ask_ticks = 2
    quote = native.Quote()
    quote.instrument_id = 42
    quote.bid_ticks = 10
    quote.ask_ticks = 12

    intents = strategy.decide(99, [other, quote], params)
    assert len(intents) == 1
    intent = intents[0]
    assert intent.strategy_id == 7
    assert intent.instrument_id == 42
    assert intent.side == native.Side.Buy
    assert intent.qty_lots == 2
    assert intent.price_ticks == 12
    assert intent.time_in_force == native.TimeInForce.ImmediateOrCancel
    assert intent.param_version == 4
    assert intent.snapshot_sequence == 99

    params.qty_lots = 0
    assert strategy.decide(99, [quote], params) == []
