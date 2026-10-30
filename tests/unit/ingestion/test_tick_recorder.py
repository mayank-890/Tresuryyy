"""T-0102 / T-0106: tick recorder writes valid rotating journal segments (fixtures only)."""

from __future__ import annotations

import asyncio
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
SERVICES_SRC = REPO_ROOT / "services" / "src"
sys.path.insert(0, str(SERVICES_SRC))
JOURNAL_INSPECT = REPO_ROOT / "tools" / "journal_inspect"
sys.path.insert(0, str(JOURNAL_INSPECT))
TOOLS = REPO_ROOT / "tools"
sys.path.insert(0, str(TOOLS))

from journal_retention.footer import FOOTER_SIZE, segment_has_footer  # noqa: E402
from segment import InspectStatus, inspect_segment  # noqa: E402
from tradelab_services.ingestion.recorder import (  # noqa: E402
    RecorderConfig,
    run_mock_ws_session,
    run_recording_session,
)
from tradelab_services.journal import generated as g  # noqa: E402


def _first_segment(root: Path) -> Path:
    segments = sorted(root.glob("*.journal"))
    assert segments, "expected at least one journal segment"
    return segments[0]


def _segment_body_bytes(segment: Path) -> bytes:
    data = segment.read_bytes()
    if segment_has_footer(str(segment)):
        return data[:-FOOTER_SIZE]
    return data


def test_mock_recording_writes_inspectable_journal(tmp_path: Path) -> None:
    config = RecorderConfig(
        journal_root=tmp_path,
        instruments_file=REPO_ROOT / "tests/fixtures/ingestion/recording_instruments.toml",
        mock_feed_file=REPO_ROOT / "tests/fixtures/delta/public_feed.ndjson",
    )
    assert run_recording_session(config) == 0

    segment = _first_segment(tmp_path)
    assert segment_has_footer(str(segment))
    report = inspect_segment(_segment_body_bytes(segment))
    assert report.status == InspectStatus.OK
    assert len(report.records) >= 5

    types = {r.header.record_type for r in report.records}
    assert int(g.RecordType.SessionStart) in types
    assert int(g.RecordType.MarketQuote) in types
    assert int(g.RecordType.MarketTrade) in types
    assert int(g.RecordType.FeedState) in types
    assert int(g.RecordType.SessionEnd) in types


def test_sequence_gap_emits_feed_state(tmp_path: Path) -> None:
    config = RecorderConfig(
        journal_root=tmp_path,
        instruments_file=REPO_ROOT / "tests/fixtures/ingestion/recording_instruments.toml",
        mock_feed_file=REPO_ROOT / "tests/fixtures/delta/public_feed.ndjson",
    )
    run_recording_session(config)
    segment = _first_segment(tmp_path)
    feed_states = [
        r
        for r in inspect_segment(_segment_body_bytes(segment)).records
        if r.header.record_type == int(g.RecordType.FeedState)
    ]
    assert feed_states
    payload = g.decode_feed_state(feed_states[0].payload)
    assert payload.reason_code == 1


def test_mock_ws_mode_writes_footer(tmp_path: Path) -> None:
    config = RecorderConfig(
        journal_root=tmp_path,
        instruments_file=REPO_ROOT / "tests/fixtures/ingestion/recording_instruments.toml",
        mock_feed_file=REPO_ROOT / "tests/fixtures/delta/public_feed.ndjson",
    )
    rc = asyncio.run(run_mock_ws_session(config))
    assert rc == 0
    assert segment_has_footer(str(_first_segment(tmp_path)))


def test_empty_instrument_list_refuses_connect(tmp_path: Path) -> None:
    empty_toml = tmp_path / "empty.toml"
    empty_toml.write_text("instrument = []\n", encoding="utf-8")
    config = RecorderConfig(
        journal_root=tmp_path / "root",
        instruments_file=empty_toml,
        mock_feed_file=REPO_ROOT / "tests/fixtures/delta/public_feed.ndjson",
    )
    assert run_recording_session(config) == 2
