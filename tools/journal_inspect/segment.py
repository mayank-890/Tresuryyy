"""Read-only v1 journal segment validation and printing."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from typing import Callable

from _generated import load_generated

g = load_generated()

RECORD_HEADER_SIZE = g.RECORD_HEADER_SIZE
JOURNAL_MAGIC = g.JOURNAL_MAGIC


class InspectStatus(IntEnum):
    OK = 0
    TORN_TAIL = 1
    FATAL = 2


@dataclass(frozen=True)
class ParsedRecord:
    offset: int
    header: g.RecordHeader
    payload: bytes

    @property
    def total_size(self) -> int:
        return RECORD_HEADER_SIZE + self.header.payload_length


@dataclass
class InspectReport:
    status: InspectStatus
    records: list[ParsedRecord]
    message: str
    error_offset: int | None = None
    alert_code: str | None = None


def _record_type_name(record_type: int) -> str:
    for member in g.RecordType:
        if int(member) == record_type:
            return member.name
    return f"Unknown(0x{record_type:04x})"


def _validate_crc(header: g.RecordHeader, payload: bytes) -> bool:
    import struct

    encoded = g.encode_header(
        g.RecordHeader(
            magic=header.magic,
            format_version=header.format_version,
            record_type=header.record_type,
            sequence=header.sequence,
            monotonic_ns=header.monotonic_ns,
            wall_ns=header.wall_ns,
            payload_length=header.payload_length,
            crc32c=0,
        ),
        payload,
    )
    (expected,) = struct.unpack_from("<I", encoded, 36)
    return header.crc32c == expected


def inspect_segment(data: bytes) -> InspectReport:
    """Scan a segment forward; do not modify *data*."""
    records: list[ParsedRecord] = []
    offset = 0
    expected_sequence: int | None = None

    while offset < len(data):
        remaining = len(data) - offset
        if remaining < RECORD_HEADER_SIZE:
            return InspectReport(
                status=InspectStatus.TORN_TAIL,
                records=records,
                message=f"torn tail: incomplete header ({remaining} bytes) at offset {offset}",
                error_offset=offset,
                alert_code="AL-14",
            )

        try:
            header = g.decode_header(data[offset : offset + RECORD_HEADER_SIZE])
        except ValueError as exc:
            return InspectReport(
                status=InspectStatus.FATAL,
                records=records,
                message=f"invalid header at offset {offset}: {exc}",
                error_offset=offset,
            )

        if header.magic != JOURNAL_MAGIC.encode("ascii"):
            return InspectReport(
                status=InspectStatus.FATAL,
                records=records,
                message=f"bad magic at offset {offset}",
                error_offset=offset,
            )
        if header.format_version != g.FORMAT_VERSION:
            return InspectReport(
                status=InspectStatus.FATAL,
                records=records,
                message=f"unsupported format version {header.format_version} at offset {offset}",
                error_offset=offset,
            )

        record_end = offset + RECORD_HEADER_SIZE + header.payload_length
        if record_end > len(data):
            return InspectReport(
                status=InspectStatus.TORN_TAIL,
                records=records,
                message=(
                    f"torn tail: incomplete payload for sequence {header.sequence} "
                    f"at offset {offset} (need {header.payload_length} bytes, "
                    f"have {len(data) - offset - RECORD_HEADER_SIZE})"
                ),
                error_offset=offset,
                alert_code="AL-14",
            )

        payload = data[offset + RECORD_HEADER_SIZE : record_end]
        if not _validate_crc(header, payload):
            return InspectReport(
                status=InspectStatus.FATAL,
                records=records,
                message=f"CRC32C mismatch at offset {offset}",
                error_offset=offset,
            )

        if expected_sequence is not None and header.sequence != expected_sequence:
            return InspectReport(
                status=InspectStatus.FATAL,
                records=records,
                message=(
                    f"sequence gap at offset {offset}: expected {expected_sequence}, "
                    f"got {header.sequence}"
                ),
                error_offset=offset,
            )
        expected_sequence = header.sequence + 1

        records.append(ParsedRecord(offset=offset, header=header, payload=payload))
        offset = record_end

    return InspectReport(
        status=InspectStatus.OK,
        records=records,
        message=f"ok: {len(records)} record(s)",
    )


def _format_payload(record_type: int, payload: bytes) -> str:
    decoders: dict[int, Callable[[bytes], object]] = {
        int(g.RecordType.SessionStart): g.decode_session_start,
        int(g.RecordType.SessionEnd): g.decode_session_end,
        int(g.RecordType.ConfigRecorded): g.decode_config_recorded,
    }
    decode = decoders.get(record_type)
    if decode is None:
        return f"<{len(payload)} bytes>"
    try:
        value = decode(payload)
        return repr(value)
    except ValueError:
        return f"<{len(payload)} bytes, decode error>"


def format_record(rec: ParsedRecord) -> str:
    h = rec.header
    rname = _record_type_name(h.record_type)
    payload_summary = _format_payload(h.record_type, rec.payload)
    return (
        f"seq={h.sequence} type={rname} offset={rec.offset} "
        f"wall_ns={h.wall_ns} mono_ns={h.monotonic_ns} payload={payload_summary}"
    )


def filter_records(
    records: list[ParsedRecord],
    *,
    record_type: int | None = None,
    seq_min: int | None = None,
    seq_max: int | None = None,
) -> list[ParsedRecord]:
    out: list[ParsedRecord] = []
    for rec in records:
        seq = rec.header.sequence
        if record_type is not None and rec.header.record_type != record_type:
            continue
        if seq_min is not None and seq < seq_min:
            continue
        if seq_max is not None and seq > seq_max:
            continue
        out.append(rec)
    return out


def parse_record_type(name: str) -> int:
    upper = name.strip()
    for member in g.RecordType:
        if member.name.lower() == upper.lower():
            return int(member)
    raise ValueError(f"unknown record type {name!r}")
