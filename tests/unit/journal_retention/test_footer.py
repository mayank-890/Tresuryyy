"""Segment footer decode (matches C++ layout)."""

from __future__ import annotations

import struct

from journal_retention.footer import FOOTER_MAGIC, FOOTER_SIZE, decode_footer, _crc32c


def test_decode_valid_footer() -> None:
    record_count = 3
    last_sequence = 10
    body_crc = 0xAABBCCDD
    header = struct.pack("<4sHHQQI", FOOTER_MAGIC, 1, 0, record_count, last_sequence, body_crc)
    footer_crc = _crc32c(header)
    blob = header + struct.pack("<I", footer_crc)
    assert len(blob) == FOOTER_SIZE
    parsed = decode_footer(blob)
    assert parsed is not None
    assert parsed.record_count == record_count
    assert parsed.last_sequence == last_sequence
    assert parsed.body_crc32c == body_crc


def test_decode_rejects_bad_magic() -> None:
    assert decode_footer(b"\x00" * FOOTER_SIZE) is None
