"""Fixture tests for journal inspector (T-0008)."""

from __future__ import annotations

import hashlib
import subprocess
import sys
import unittest
from pathlib import Path

_INSPECT_DIR = Path(__file__).resolve().parent
_ROOT = _INSPECT_DIR.parents[1]
sys.path.insert(0, str(_INSPECT_DIR))

from segment import (  # noqa: E402
    InspectStatus,
    filter_records,
    inspect_segment,
    parse_record_type,
)

FIXTURES = _ROOT / "tests/fixtures/compat/v1"
CLI = [_ROOT / "tools/journal_inspect/__main__.py"]


class JournalInspectTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        subprocess.run(
            [sys.executable, str(_INSPECT_DIR / "build_fixtures.py")],
            check=True,
            cwd=_ROOT,
        )

    def test_normal_segment(self) -> None:
        path = FIXTURES / "normal.segment"
        data = path.read_bytes()
        report = inspect_segment(data)
        self.assertEqual(report.status, InspectStatus.OK)
        self.assertEqual(len(report.records), 2)
        self.assertEqual(report.records[0].header.sequence, 1)
        self.assertEqual(report.records[1].header.sequence, 2)

    def test_torn_tail(self) -> None:
        path = FIXTURES / "torn_tail.segment"
        report = inspect_segment(path.read_bytes())
        self.assertEqual(report.status, InspectStatus.TORN_TAIL)
        self.assertEqual(report.alert_code, "AL-14")
        self.assertEqual(len(report.records), 1)

    def test_crc_corrupt_fatal_offset(self) -> None:
        path = FIXTURES / "crc_corrupt.segment"
        report = inspect_segment(path.read_bytes())
        self.assertEqual(report.status, InspectStatus.FATAL)
        self.assertIsNotNone(report.error_offset)
        self.assertIn("CRC32C", report.message)

    def test_filter_by_type_and_sequence(self) -> None:
        path = FIXTURES / "normal.segment"
        report = inspect_segment(path.read_bytes())
        session_start = parse_record_type("SessionStart")
        filtered = filter_records(
            report.records,
            record_type=session_start,
            seq_min=1,
            seq_max=1,
        )
        self.assertEqual(len(filtered), 1)
        self.assertEqual(filtered[0].header.record_type, session_start)

    def test_read_only_file_hash(self) -> None:
        for name in ("normal.segment", "torn_tail.segment", "crc_corrupt.segment"):
            path = FIXTURES / name
            before = hashlib.sha256(path.read_bytes()).hexdigest()
            inspect_segment(path.read_bytes())
            proc = subprocess.run(
                [sys.executable, str(CLI[0]), str(path)],
                capture_output=True,
                text=True,
                cwd=_ROOT,
            )
            self.assertEqual(proc.returncode, int(InspectStatus.OK if name == "normal.segment" else InspectStatus.TORN_TAIL if name == "torn_tail.segment" else InspectStatus.FATAL))
            after = hashlib.sha256(path.read_bytes()).hexdigest()
            self.assertEqual(before, after)

    def test_cli_filter(self) -> None:
        path = FIXTURES / "normal.segment"
        proc = subprocess.run(
            [
                sys.executable,
                str(CLI[0]),
                str(path),
                "--type",
                "SessionEnd",
                "--seq-min",
                "2",
                "--seq-max",
                "2",
            ],
            capture_output=True,
            text=True,
            check=True,
            cwd=_ROOT,
        )
        self.assertIn("type=SessionEnd", proc.stdout)
        self.assertNotIn("type=SessionStart", proc.stdout)


if __name__ == "__main__":
    unittest.main()
