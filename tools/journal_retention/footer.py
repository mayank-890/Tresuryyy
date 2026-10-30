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


def _crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 & -(crc & 1))
    return (~crc) & 0xFFFFFFFF


def decode_footer(blob: bytes) -> SegmentFooter | None:
    if len(blob) != FOOTER_SIZE or blob[:4] != FOOTER_MAGIC:
        return None
    version, _reserved = struct.unpack_from("<HH", blob, 4)
    if version != FOOTER_VERSION:
        return None
    record_count, last_sequence, body_crc = struct.unpack_from("<QQI", blob, 8)
    stored_crc, = struct.unpack_from("<I", blob, 28)
    temp = bytearray(blob)
    struct.pack_into("<I", temp, 28, 0)
    if _crc32c(bytes(temp[:28])) != stored_crc:
        return None
    return SegmentFooter(record_count=record_count, last_sequence=last_sequence, body_crc32c=body_crc)


def segment_has_footer(path: str) -> bool:
    with open(path, "rb") as handle:
        handle.seek(-FOOTER_SIZE, 2)
        tail = handle.read(FOOTER_SIZE)
    return decode_footer(tail) is not None
