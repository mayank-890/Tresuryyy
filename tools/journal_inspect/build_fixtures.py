"""Generate synthetic v1 journal fixtures under tests/fixtures/compat/v1/."""

from __future__ import annotations

import sys
from pathlib import Path

_INSPECT_DIR = Path(__file__).resolve().parent
_ROOT = _INSPECT_DIR.parents[1]
sys.path.insert(0, str(_INSPECT_DIR))

from _generated import load_generated  # noqa: E402

g = load_generated()
RECORD_HEADER_SIZE = g.RECORD_HEADER_SIZE
OUT = _ROOT / "tests/fixtures/compat/v1"


def _record(
    *,
    sequence: int,
    record_type: int,
    payload: bytes,
    wall_ns: int,
    monotonic_ns: int,
) -> bytes:
    header = g.RecordHeader(
        magic=b"TJNL",
        format_version=g.FORMAT_VERSION,
        record_type=record_type,
        sequence=sequence,
        monotonic_ns=monotonic_ns,
        wall_ns=wall_ns,
        payload_length=len(payload),
        crc32c=0,
    )
    return g.encode_header(header, payload) + payload


def _session_start_payload() -> bytes:
    return g.encode_session_start(
        g.SessionStartPayload(
            profile_name="paper-fixture",
            config_hash=bytes([0xAA] * 32),
            engine_build_hash=bytes([0xBB] * 32),
            client_id_prefix="FX01",
        )
    )


def _session_end_payload() -> bytes:
    return g.encode_session_end(
        g.SessionEndPayload(
            g.SessionEndReason.NormalShutdown,
            g.EngineRunState.Running,
        )
    )


def build_normal() -> bytes:
    p0 = _session_start_payload()
    p1 = _session_end_payload()
    return b"".join(
        [
            _record(
                sequence=1,
                record_type=int(g.RecordType.SessionStart),
                payload=p0,
                wall_ns=1_705_326_896_789_000_000,
                monotonic_ns=100,
            ),
            _record(
                sequence=2,
                record_type=int(g.RecordType.SessionEnd),
                payload=p1,
                wall_ns=1_705_326_900_000_000_000,
                monotonic_ns=200,
            ),
        ]
    )


def build_torn_tail(normal: bytes) -> bytes:
    # Truncate mid second record payload.
    return normal[: len(normal) - 8]


def build_crc_corrupt(normal: bytes) -> bytes:
    data = bytearray(normal)
    # Flip a byte in the first record payload (after header).
    data[RECORD_HEADER_SIZE + 5] ^= 0xFF
    return bytes(data)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    normal = build_normal()
    (OUT / "normal.segment").write_bytes(normal)
    (OUT / "torn_tail.segment").write_bytes(build_torn_tail(normal))
    (OUT / "crc_corrupt.segment").write_bytes(build_crc_corrupt(normal))
    print(f"wrote fixtures under {OUT}")


if __name__ == "__main__":
    main()
