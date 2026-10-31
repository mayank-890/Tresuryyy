"""T-0108: one idempotent projection against local PostgreSQL.

Not part of `just check` (same split as `just test-db`). Run `just projector-integration`.
"""

from __future__ import annotations

import os
import sys
import uuid
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
for _entry in (
    REPO_ROOT / "services" / "src",
    REPO_ROOT / "tools",
    REPO_ROOT / "tools" / "journal_inspect",
):
    if str(_entry) not in sys.path:
        sys.path.insert(0, str(_entry))

from tradelab_services.journal import generated as g  # noqa: E402
from tradelab_services.projector.project import project_segment, run_psql  # noqa: E402

FIXED_WALL_NS = 1_705_326_896_789_000_000


def _require_database_url() -> str:
    url = os.environ.get("DATABASE_URL", "")
    if "://" not in url:
        msg = "DATABASE_URL is not set; run just projector-integration"
        raise RuntimeError(msg)
    return url


def _record(sequence: int, record_type: int, payload: bytes) -> bytes:
    header = g.RecordHeader(
        magic=g.JOURNAL_MAGIC.encode("ascii"),
        format_version=g.FORMAT_VERSION,
        record_type=record_type,
        sequence=sequence,
        monotonic_ns=sequence,
        wall_ns=FIXED_WALL_NS,
        payload_length=len(payload),
        crc32c=0,
    )
    return g.encode_header(header, payload) + payload


def _scalar(url: str, sql: str) -> str:
    return run_psql(url, sql, tuples_only=True).strip()


def test_reproject_does_not_duplicate(tmp_path: Path) -> None:
    url = _require_database_url()
    token = uuid.uuid4().hex[:8]
    segment_name = f"t0108-{token}.journal"
    client_id = f"t0108{token}"
    run_psql(
        url,
        """
        CREATE TABLE IF NOT EXISTS trading.market_ticks_20240115
          PARTITION OF trading.market_ticks
          FOR VALUES FROM ('2024-01-15 00:00:00+00') TO ('2024-01-16 00:00:00+00');
        CREATE TABLE IF NOT EXISTS trading.order_events_20240115
          PARTITION OF trading.order_events
          FOR VALUES FROM ('2024-01-15 00:00:00+00') TO ('2024-01-16 00:00:00+00');
        CREATE TABLE IF NOT EXISTS trading.risk_events_202401
          PARTITION OF trading.risk_events
          FOR VALUES FROM ('2024-01-01 00:00:00+00') TO ('2024-02-01 00:00:00+00');
        """,
    )
    quote = g.encode_market_quote(
        g.MarketQuotePayload(
            instrument_id=7,
            bid_ticks=4242,
            ask_ticks=4243,
            bid_size_lots=1,
            ask_size_lots=1,
            exchange_time_ns=FIXED_WALL_NS,
        )
    )
    trade = g.encode_market_trade(
        g.MarketTradePayload(
            instrument_id=7,
            price_ticks=4244,
            size_lots=2,
            side=int(g.Side.Sell),
            exchange_time_ns=FIXED_WALL_NS,
        )
    )
    intent = g.encode_order_intent(
        g.OrderIntentPayload(
            client_id=client_id,
            strategy_id=4,
            instrument_id=7,
            side=int(g.Side.Buy),
            qty_lots=2,
            price_ticks=4242,
            time_in_force=int(g.TimeInForce.Day),
            param_version=1,
        )
    )
    segment = tmp_path / segment_name
    segment.write_bytes(
        _record(1, int(g.RecordType.MarketQuote), quote)
        + _record(2, int(g.RecordType.MarketTrade), trade)
        + _record(3, int(g.RecordType.OrderIntent), intent)
    )

    first = project_segment(segment, apply=True, database_url=url)
    second = project_segment(segment, apply=True, database_url=url)
    assert first.applied and second.applied
    assert first.mapped == 3

    ticks = _scalar(
        url,
        f"SELECT count(*) FROM trading.market_ticks WHERE source_segment = '{segment_name}';",
    )
    bid = _scalar(
        url,
        "SELECT bid_ticks FROM trading.market_ticks "
        f"WHERE source_segment = '{segment_name}' AND source_sequence = 1;",
    )
    event_ok = _scalar(
        url,
        "SELECT (event_time = '2024-01-15 13:54:56.789+00'::timestamptz)::text "
        "FROM trading.market_ticks "
        f"WHERE source_segment = '{segment_name}' AND source_sequence = 1;",
    )
    orders = _scalar(
        url,
        f"SELECT count(*) FROM trading.orders WHERE client_order_id = '{client_id}';",
    )
    assert ticks == "2"
    assert bid == "4242"
    assert event_ok == "true"
    assert orders == "1"

    run_psql(
        url,
        "DELETE FROM trading.market_ticks "
        f"WHERE source_segment = '{segment_name}'; "
        "DELETE FROM trading.orders "
        f"WHERE client_order_id = '{client_id}';",
    )
