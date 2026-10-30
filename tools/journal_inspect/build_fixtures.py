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


def build_sequence_gap() -> bytes:
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
                sequence=3,
                record_type=int(g.RecordType.SessionEnd),
                payload=p1,
                wall_ns=1_705_326_900_000_000_000,
                monotonic_ns=200,
            ),
        ]
    )


def build_market_sample() -> bytes:
    quote_a = g.encode_market_quote(
        g.MarketQuotePayload(
            instrument_id=1,
            bid_ticks=100,
            ask_ticks=101,
            bid_size_lots=1,
            ask_size_lots=1,
            exchange_time_ns=1_000,
        )
    )
    quote_b = g.encode_market_quote(
        g.MarketQuotePayload(
            instrument_id=1,
            bid_ticks=102,
            ask_ticks=103,
            bid_size_lots=1,
            ask_size_lots=1,
            exchange_time_ns=2_000,
        )
    )
    quote_c = g.encode_market_quote(
        g.MarketQuotePayload(
            instrument_id=1,
            bid_ticks=104,
            ask_ticks=105,
            bid_size_lots=1,
            ask_size_lots=1,
            exchange_time_ns=8_000_000_000,
        )
    )
    return b"".join(
        [
            _record(
                sequence=1,
                record_type=int(g.RecordType.SessionStart),
                payload=_session_start_payload(),
                wall_ns=0,
                monotonic_ns=0,
            ),
            _record(
                sequence=2,
                record_type=int(g.RecordType.MarketQuote),
                payload=quote_a,
                wall_ns=1_000_000_000,
                monotonic_ns=1_000_000_000,
            ),
            _record(
                sequence=3,
                record_type=int(g.RecordType.MarketQuote),
                payload=quote_b,
                wall_ns=2_000_000_000,
                monotonic_ns=2_000_000_000,
            ),
            _record(
                sequence=4,
                record_type=int(g.RecordType.MarketQuote),
                payload=quote_c,
                wall_ns=8_000_000_000,
                monotonic_ns=8_000_000_000,
            ),
            _record(
                sequence=5,
                record_type=int(g.RecordType.SessionEnd),
                payload=_session_end_payload(),
                wall_ns=8_000_000_001,
                monotonic_ns=8_000_000_001,
            ),
        ]
    )


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    normal = build_normal()
    (OUT / "normal.segment").write_bytes(normal)
    (OUT / "torn_tail.segment").write_bytes(build_torn_tail(normal))
    (OUT / "crc_corrupt.segment").write_bytes(build_crc_corrupt(normal))
    (OUT / "sequence_gap.segment").write_bytes(build_sequence_gap())
    (OUT / "market_sample.segment").write_bytes(build_market_sample())
    print(f"wrote fixtures under {OUT}")


if __name__ == "__main__":
    main()
