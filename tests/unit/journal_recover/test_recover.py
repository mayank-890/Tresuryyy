"""T-0107: torn-tail recovery on synthetic segments (recording path only)."""

from __future__ import annotations

import hashlib
import os
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[3]
TOOLS = REPO_ROOT / "tools"
INSPECT = TOOLS / "journal_inspect"
SERVICES_SRC = REPO_ROOT / "services" / "src"
FIXTURES = REPO_ROOT / "tests/fixtures/compat/v1"
KILL_CYCLES = 100

for _entry in (str(TOOLS), str(INSPECT), str(SERVICES_SRC)):
    if _entry not in sys.path:
        sys.path.insert(0, _entry)

from journal_recover.recover import (  # noqa: E402
    RecoveryAction,
    apply_recovery,
    plan_recovery,
)
from segment import InspectStatus, inspect_segment  # noqa: E402
from tradelab_services.journal import generated as g  # noqa: E402
from tradelab_services.journal.segment_footer import SegmentFooter, encode_footer  # noqa: E402
from tradelab_services.journal.writer import JournalWriter  # noqa: E402


@pytest.fixture(scope="module", autouse=True)
def _build_fixtures() -> None:
    subprocess.run(
        [sys.executable, str(INSPECT / "build_fixtures.py")],
        check=True,
        cwd=REPO_ROOT,
    )


def _payload() -> bytes:
    return g.encode_session_start(
        g.SessionStartPayload(
            profile_name="recovery-smoke",
            config_hash=bytes([0x11] * 32),
            engine_build_hash=bytes([0x22] * 32),
            client_id_prefix="RCV1",
        )
    )


def _full_record(sequence: int, payload: bytes) -> bytes:
    header = g.RecordHeader(
        magic=g.JOURNAL_MAGIC.encode("ascii"),
        format_version=g.FORMAT_VERSION,
        record_type=int(g.RecordType.SessionStart),
        sequence=sequence,
        monotonic_ns=1000 + sequence,
        wall_ns=1_705_326_896_789_000_000 + sequence,
        payload_length=len(payload),
        crc32c=0,
    )
    return g.encode_header(header, payload) + payload


def _kill_after_durable_prefix(path: Path, complete: int, tear_len: int) -> bytes:
    """Fsync `complete` records, then append a partial next record (kill during write)."""
    payload = _payload()
    writer = JournalWriter(path=path, next_sequence=1)
    writer.open()
    for index in range(complete):
        writer.append(
            g.RecordType.SessionStart,
            payload,
            monotonic_ns=1000 + index,
            wall_ns=1_705_326_896_789_000_000 + index,
        )
    writer.flush()
    durable = path.read_bytes()
    writer.close()
    nxt = _full_record(complete + 1, payload)
    if not 0 < tear_len < len(nxt):
        msg = f"tear_len {tear_len} out of range for record length {len(nxt)}"
        raise AssertionError(msg)
    with path.open("ab") as handle:
        handle.write(nxt[:tear_len])
    return durable


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test_fixture_torn_tail_apply_keeps_prefix(tmp_path: Path) -> None:
    source = FIXTURES / "torn_tail.segment"
    segment = tmp_path / "torn_tail.segment"
    original = source.read_bytes()
    segment.write_bytes(original)

    prefix, planned = plan_recovery(original)
    assert planned.action is RecoveryAction.DRY_RUN
    assert planned.alert_code == "AL-14"
    assert planned.records_kept == 1
    assert prefix is not None
    assert prefix == original[: planned.valid_prefix_length]

    dry = apply_recovery(segment, apply=False)
    assert dry.action is RecoveryAction.DRY_RUN
    assert segment.read_bytes() == original

    applied = apply_recovery(segment, apply=True)
    assert applied.action is RecoveryAction.APPLIED
    assert applied.alert_code == "AL-14"
    assert applied.backup_path is not None
    assert applied.backup_path.read_bytes() == original
    recovered = segment.read_bytes()
    assert recovered == prefix
    report = inspect_segment(recovered)
    assert report.status is InspectStatus.OK
    assert len(report.records) == 1
    assert report.records[0].header.sequence == 1


def test_dry_run_cli_does_not_modify(tmp_path: Path) -> None:
    segment = tmp_path / "torn.segment"
    segment.write_bytes((FIXTURES / "torn_tail.segment").read_bytes())
    before = _sha256(segment)
    proc = subprocess.run(
        [sys.executable, "-m", "journal_recover", str(segment)],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
        env={**os.environ, "PYTHONPATH": f"{TOOLS}:{INSPECT}"},
        check=False,
    )
    assert proc.returncode == 1
    assert "alert=AL-14" in proc.stdout
    assert "action=dry-run" in proc.stdout
    assert _sha256(segment) == before
    assert not (tmp_path / "torn.segment.torn").exists()


def test_apply_cli_then_inspect_ok(tmp_path: Path) -> None:
    segment = tmp_path / "torn.segment"
    segment.write_bytes((FIXTURES / "torn_tail.segment").read_bytes())
    env = {**os.environ, "PYTHONPATH": f"{TOOLS}:{INSPECT}"}
    proc = subprocess.run(
        [sys.executable, "-m", "journal_recover", str(segment), "--apply"],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
        env=env,
        check=False,
    )
    assert proc.returncode == 0
    assert "action=applied" in proc.stdout
    assert "alert=AL-14" in proc.stdout
    inspect = subprocess.run(
        [sys.executable, str(INSPECT / "__main__.py"), str(segment)],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
        check=False,
    )
    assert inspect.returncode == 0
    assert "status=OK" in inspect.stdout


def test_crc_corrupt_is_not_truncated(tmp_path: Path) -> None:
    segment = tmp_path / "crc.segment"
    segment.write_bytes((FIXTURES / "crc_corrupt.segment").read_bytes())
    before = segment.read_bytes()
    report = apply_recovery(segment, apply=True)
    assert report.action is RecoveryAction.REFUSED
    assert report.inspector_status is InspectStatus.FATAL
    assert segment.read_bytes() == before
    assert not (tmp_path / "crc.segment.torn").exists()


def test_sequence_gap_is_not_truncated(tmp_path: Path) -> None:
    segment = tmp_path / "gap.segment"
    segment.write_bytes((FIXTURES / "sequence_gap.segment").read_bytes())
    before = segment.read_bytes()
    report = apply_recovery(segment, apply=True)
    assert report.action is RecoveryAction.REFUSED
    assert "sequence gap" in report.message
    assert segment.read_bytes() == before


def test_valid_footer_is_not_stripped(tmp_path: Path) -> None:
    payload = _payload()
    body = _full_record(1, payload)
    footer = encode_footer(SegmentFooter(record_count=1, last_sequence=1, body_crc32c=0))
    segment = tmp_path / "closed.journal"
    segment.write_bytes(body + footer)
    before = segment.read_bytes()
    report = apply_recovery(segment, apply=True)
    assert report.action is RecoveryAction.NONE
    assert report.alert_code is None
    assert report.records_kept == 1
    assert segment.read_bytes() == before
    assert not (tmp_path / "closed.journal.torn").exists()


def test_partial_footer_magic_is_torn_and_removed(tmp_path: Path) -> None:
    payload = _payload()
    durable = _full_record(1, payload)
    segment = tmp_path / "partial-footer.journal"
    segment.write_bytes(durable + b"TJN")
    applied = apply_recovery(segment, apply=True)
    assert applied.action is RecoveryAction.APPLIED
    assert applied.alert_code == "AL-14"
    assert segment.read_bytes() == durable
    assert inspect_segment(segment.read_bytes()).status is InspectStatus.OK


def test_backup_mismatch_refuses_second_truncate(tmp_path: Path) -> None:
    segment = tmp_path / "torn.segment"
    original = (FIXTURES / "torn_tail.segment").read_bytes()
    segment.write_bytes(original)
    assert apply_recovery(segment, apply=True).action is RecoveryAction.APPLIED
    segment.write_bytes(original)
    backup = tmp_path / "torn.segment.torn"
    assert backup.read_bytes() == original
    backup.write_bytes(b"different-audit-copy")
    refused = apply_recovery(segment, apply=True)
    assert refused.action is RecoveryAction.REFUSED
    assert segment.read_bytes() == original


def test_kill_during_write_recovers_prefix_every_cycle(tmp_path: Path) -> None:
    payload = _payload()
    record = _full_record(1, payload)
    tear_choices = (1, g.RECORD_HEADER_SIZE - 1, g.RECORD_HEADER_SIZE, len(record) - 1)
    for cycle in range(KILL_CYCLES):
        complete = (cycle % 4) + 1
        tear_len = tear_choices[cycle % len(tear_choices)]
        segment = tmp_path / f"cycle-{cycle}.journal"
        durable = _kill_after_durable_prefix(segment, complete, tear_len)
        torn = segment.read_bytes()
        assert torn.startswith(durable)
        assert len(torn) == len(durable) + tear_len
        assert inspect_segment(torn).status is InspectStatus.TORN_TAIL

        applied = apply_recovery(segment, apply=True)
        assert applied.action is RecoveryAction.APPLIED, cycle
        assert applied.alert_code == "AL-14"
        assert segment.read_bytes() == durable
        recovered = inspect_segment(segment.read_bytes())
        assert recovered.status is InspectStatus.OK
        assert len(recovered.records) == complete
        assert [rec.header.sequence for rec in recovered.records] == list(range(1, complete + 1))
        backup = segment.parent / f"{segment.name}.torn"
        assert backup.read_bytes() == torn
