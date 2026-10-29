"""T-0102: tick recorder writes valid journal segments (fixtures only)."""

from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
SERVICES_SRC = REPO_ROOT / "services" / "src"
sys.path.insert(0, str(SERVICES_SRC))
JOURNAL_INSPECT = REPO_ROOT / "tools" / "journal_inspect"
sys.path.insert(0, str(JOURNAL_INSPECT))

from segment import InspectStatus, inspect_segment  # noqa: E402
from tradelab_services.ingestion.recorder import RecorderConfig, run_recording_session  # noqa: E402
from tradelab_services.journal import generated as g  # noqa: E402


def test_mock_recording_writes_inspectable_journal(tmp_path: Path) -> None:
    journal_path = tmp_path / "capture.journal"
    config = RecorderConfig(
        journal_path=journal_path,
        instruments_file=REPO_ROOT / "tests/fixtures/ingestion/recording_instruments.toml",
        mock_feed_file=REPO_ROOT / "tests/fixtures/delta/public_feed.ndjson",
    )
    assert run_recording_session(config) == 0

    data = journal_path.read_bytes()
    report = inspect_segment(data)
    assert report.status == InspectStatus.OK
    assert len(report.records) >= 5

    types = {r.header.record_type for r in report.records}
    assert int(g.RecordType.SessionStart) in types
    assert int(g.RecordType.MarketQuote) in types
    assert int(g.RecordType.MarketTrade) in types
    assert int(g.RecordType.FeedState) in types
    assert int(g.RecordType.SessionEnd) in types


def test_sequence_gap_emits_feed_state(tmp_path: Path) -> None:
    journal_path = tmp_path / "gap.journal"
    config = RecorderConfig(
        journal_path=journal_path,
        instruments_file=REPO_ROOT / "tests/fixtures/ingestion/recording_instruments.toml",
        mock_feed_file=REPO_ROOT / "tests/fixtures/delta/public_feed.ndjson",
    )
    run_recording_session(config)
    feed_states = [
        r
        for r in inspect_segment(journal_path.read_bytes()).records
        if r.header.record_type == int(g.RecordType.FeedState)
    ]
    assert feed_states
    payload = g.decode_feed_state(feed_states[0].payload)
    assert payload.reason_code == 1
