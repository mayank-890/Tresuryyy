"""T-0103: journal data-quality report tests (fixtures only)."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[3]
FIXTURES = REPO_ROOT / "tests/fixtures/compat/v1"
TOOLS = REPO_ROOT / "tools"
PYTHONPATH = f"{TOOLS}:{TOOLS / 'journal_inspect'}"


@pytest.fixture(scope="module", autouse=True)
def _build_fixtures() -> None:
    subprocess.run(
        [sys.executable, str(TOOLS / "journal_inspect/build_fixtures.py")],
        check=True,
        cwd=REPO_ROOT,
    )


def _report(path: Path, *, stale_ms: float = 5000.0) -> dict[str, object]:
    sys.path.insert(0, str(TOOLS))
    sys.path.insert(0, str(TOOLS / "journal_inspect"))
    from journal_dq_report.analyze import build_report

    data = path.read_bytes()
    rep = build_report(path, data, stale_threshold_ns=int(stale_ms * 1_000_000))
    return json.loads(rep.to_json())


def test_normal_segment_ok() -> None:
    rep = _report(FIXTURES / "normal.segment")
    assert rep["structural_status"] == "OK"
    assert rep["sequence_gap_count"] == 0
    assert rep["record_count"] == 2


def test_sequence_gap_fixture() -> None:
    rep = _report(FIXTURES / "sequence_gap.segment")
    assert rep["sequence_gap_count"] == 1
    gap = rep["sequence_gaps"][0]
    assert gap["expected_sequence"] == 2
    assert gap["actual_sequence"] == 3


def test_crc_corrupt_reported() -> None:
    rep = _report(FIXTURES / "crc_corrupt.segment")
    assert rep["structural_status"] == "FATAL"
    assert "CRC32C" in rep["structural_message"]


def test_torn_tail_alert() -> None:
    rep = _report(FIXTURES / "torn_tail.segment")
    assert rep["structural_status"] == "TORN_TAIL"
    assert rep["alert_code"] == "AL-14"


def test_market_stale_gap_stats() -> None:
    rep = _report(FIXTURES / "market_sample.segment", stale_ms=1000.0)
    assert rep["stale_period_count"] >= 1
    inst = rep["instruments"][0]
    assert inst["quote_count"] == 3
    assert inst["max_inter_event_gap_ns"] >= 5_999_000_000


def test_cli_json_output() -> None:
    proc = subprocess.run(
        [
            sys.executable,
            "-m",
            "journal_dq_report",
            str(FIXTURES / "normal.segment"),
            "--format",
            "json",
        ],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
        env={"PYTHONPATH": PYTHONPATH, **__import__("os").environ},
        check=True,
    )
    payload = json.loads(proc.stdout)
    assert payload["structural_status"] == "OK"
