"""Segment footer layout (must match engine/src/journal/segment_footer.cpp)."""

from __future__ import annotations

import struct
from dataclasses import dataclass

FOOTER_MAGIC = b"TJNF"
FOOTER_VERSION = 1
FOOTER_SIZE = 32


@dataclass(frozen=True, slots=True)
class SegmentFooter:
    record_count: int
    last_sequence: int
    body_crc32c: int


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 & -(crc & 1))
    return (~crc) & 0xFFFFFFFF


def encode_footer(footer: SegmentFooter) -> bytes:
    header = struct.pack(
        "<4sHHQQI",
        FOOTER_MAGIC,
        FOOTER_VERSION,
        0,
        footer.record_count,
        footer.last_sequence,
        footer.body_crc32c,
    )
    footer_crc = crc32c(header)
    return header + struct.pack("<I", footer_crc)
