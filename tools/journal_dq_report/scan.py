"""Lenient v1 segment scan for data-quality analysis (read-only)."""

from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path

_INSPECT = Path(__file__).resolve().parents[1] / "journal_inspect"
if str(_INSPECT) not in sys.path:
    sys.path.insert(0, str(_INSPECT))

from segment import (  # noqa: E402
    RECORD_HEADER_SIZE,
    JOURNAL_MAGIC,
    InspectReport,
    InspectStatus,
    ParsedRecord,
    _validate_crc,
    inspect_segment,
)

from _generated import load_generated  # noqa: E402

g = load_generated()


@dataclass(frozen=True, slots=True)
class SequenceGapFinding:
    expected_sequence: int
    actual_sequence: int
    offset: int


@dataclass
class LenientScanResult:
    records: list[ParsedRecord]
    sequence_gaps: list[SequenceGapFinding]
    structural: InspectReport


def scan_segment_lenient(data: bytes) -> LenientScanResult:
    """Parse records; record sequence gaps and continue; stop on CRC/torn tail."""
    records: list[ParsedRecord] = []
    sequence_gaps: list[SequenceGapFinding] = []
    offset = 0
    expected_sequence: int | None = None

    while offset < len(data):
        remaining = len(data) - offset
        if remaining < RECORD_HEADER_SIZE:
            return LenientScanResult(
                records=records,
                sequence_gaps=sequence_gaps,
                structural=InspectReport(
                    status=InspectStatus.TORN_TAIL,
                    records=records,
                    message=f"torn tail: incomplete header ({remaining} bytes) at offset {offset}",
                    error_offset=offset,
                    alert_code="AL-14",
                ),
            )

        try:
            header = g.decode_header(data[offset : offset + RECORD_HEADER_SIZE])
        except ValueError as exc:
            return LenientScanResult(
                records=records,
                sequence_gaps=sequence_gaps,
                structural=InspectReport(
                    status=InspectStatus.FATAL,
                    records=records,
                    message=f"invalid header at offset {offset}: {exc}",
                    error_offset=offset,
                ),
            )

        if header.magic != JOURNAL_MAGIC.encode("ascii"):
            return LenientScanResult(
                records=records,
                sequence_gaps=sequence_gaps,
                structural=InspectReport(
                    status=InspectStatus.FATAL,
                    records=records,
                    message=f"bad magic at offset {offset}",
                    error_offset=offset,
                ),
            )

        record_end = offset + RECORD_HEADER_SIZE + header.payload_length
        if record_end > len(data):
            return LenientScanResult(
                records=records,
                sequence_gaps=sequence_gaps,
                structural=InspectReport(
                    status=InspectStatus.TORN_TAIL,
                    records=records,
                    message=(
                        f"torn tail: incomplete payload for sequence {header.sequence} "
                        f"at offset {offset}"
                    ),
                    error_offset=offset,
                    alert_code="AL-14",
                ),
            )

        payload = data[offset + RECORD_HEADER_SIZE : record_end]
        if not _validate_crc(header, payload):
            return LenientScanResult(
                records=records,
                sequence_gaps=sequence_gaps,
                structural=InspectReport(
                    status=InspectStatus.FATAL,
                    records=records,
                    message=f"CRC32C mismatch at offset {offset}",
                    error_offset=offset,
                ),
            )

        if expected_sequence is not None and header.sequence != expected_sequence:
            sequence_gaps.append(
                SequenceGapFinding(
                    expected_sequence=expected_sequence,
                    actual_sequence=header.sequence,
                    offset=offset,
                )
            )
        expected_sequence = header.sequence + 1

        records.append(ParsedRecord(offset=offset, header=header, payload=payload))
        offset = record_end

    return LenientScanResult(
        records=records,
        sequence_gaps=sequence_gaps,
        structural=InspectReport(
            status=InspectStatus.OK,
            records=records,
            message=f"ok: {len(records)} record(s)",
        ),
    )


def strict_structural_check(data: bytes) -> InspectReport:
    """Delegate torn-tail / CRC checks to the T-0008 inspector."""
    return inspect_segment(data)
