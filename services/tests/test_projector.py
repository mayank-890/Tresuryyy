"""T-0108: projector stub unit tests (no database)."""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
for _entry in (
    REPO_ROOT / "services" / "src",
    REPO_ROOT / "tools",
    REPO_ROOT / "tools" / "journal_inspect",
):
    if str(_entry) not in sys.path:
        sys.path.insert(0, str(_entry))

from segment import ParsedRecord  # noqa: E402
from tradelab_services.journal import generated as g  # noqa: E402
from tradelab_services.projector.project import (  # noqa: E402
    DatabaseUnavailable,
    event_time_literal,
    plan_records,
    project_segment,
    sql_script,
)

FIXED_WALL_NS = 1_705_326_896_789_000_000
FIXTURES = REPO_ROOT / "tests" / "fixtures" / "compat" / "v1"


def _payload_quote(bid: int = 10, ask: int = 11) -> bytes:
    return g.encode_market_quote(
        g.MarketQuotePayload(
            instrument_id=7,
            bid_ticks=bid,
            ask_ticks=ask,
            bid_size_lots=1,
            ask_size_lots=1,
            exchange_time_ns=FIXED_WALL_NS,
        )
    )


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


def _parsed(sequence: int, record_type: int, payload: bytes) -> ParsedRecord:
    blob = _record(sequence, record_type, payload)
    header = g.decode_header(blob[: g.RECORD_HEADER_SIZE])
    return ParsedRecord(offset=0, header=header, payload=payload)


def _write(path: Path, chunks: list[bytes]) -> None:
    path.write_bytes(b"".join(chunks))


def test_event_time_matches_journal_wall_ns() -> None:
    assert event_time_literal(FIXED_WALL_NS) == "2024-01-15 13:54:56.789000+00"


def test_out_of_order_records_are_sorted_and_gaps_are_not_filled() -> None:
    later = _parsed(4, int(g.RecordType.MarketQuote), _payload_quote(40, 41))
    earlier = _parsed(1, int(g.RecordType.MarketQuote), _payload_quote(10, 11))
    plan = plan_records("seg.journal", [later, earlier])
    assert plan.refused is None
    assert len(plan.gaps) == 1
    assert plan.gaps[0].expected_sequence == 2
    assert plan.gaps[0].actual_sequence == 4
    script = sql_script(plan)
    assert script.index("', 1, '") < script.index("', 4, '")
    assert "', 2, '" not in script
    assert "', 3, '" not in script
    assert "ON CONFLICT (source_segment, source_sequence, event_time) DO NOTHING" in script
    assert "now()" not in script.split("projection_checkpoint")[0]


def test_duplicate_sequence_is_refused() -> None:
    one = _parsed(1, int(g.RecordType.MarketQuote), _payload_quote())
    plan = plan_records("seg.journal", [one, one])
    assert plan.refused is not None
    assert sql_script(plan) == ""


def test_core_record_sql_uses_conflict_clauses_and_wall_time(tmp_path: Path) -> None:
    quote = _payload_quote()
    trade = g.encode_market_trade(
        g.MarketTradePayload(
            instrument_id=7,
            price_ticks=12,
            size_lots=3,
            side=int(g.Side.Buy),
            exchange_time_ns=FIXED_WALL_NS,
        )
    )
    intent = g.encode_order_intent(
        g.OrderIntentPayload(
            client_id="O'Hare",
            strategy_id=4,
            instrument_id=7,
            side=int(g.Side.Buy),
            qty_lots=2,
            price_ticks=12,
            time_in_force=int(g.TimeInForce.Day),
            param_version=1,
        )
    )
    event = g.encode_order_event(
        g.OrderEventPayload(
            client_id="O'Hare",
            from_state=int(g.OrderState.IntentJournaled),
            to_state=int(g.OrderState.Acked),
            broker_order_id="B1",
            reason_code=0,
        )
    )
    risk = g.encode_risk_rejected(
        g.RiskRejectedPayload(client_id="O'Hare", limit_id=1, observed=2, threshold=1)
    )
    command = g.encode_control_command(
        g.ControlCommandPayload(
            command=int(g.ControlCommandKind.Halt),
            scope=int(g.ControlScope.Global),
            operator_ref="op-1",
            outcome=int(g.CommandOutcome.Accepted),
        )
    )
    start = g.encode_session_start(
        g.SessionStartPayload(
            profile_name="paper",
            config_hash=bytes([0xAA] * 32),
            engine_build_hash=bytes([0xBB] * 32),
            client_id_prefix="T108",
        )
    )
    segment = tmp_path / "core.journal"
    _write(
        segment,
        [
            _record(1, int(g.RecordType.SessionStart), start),
            _record(2, int(g.RecordType.MarketQuote), quote),
            _record(3, int(g.RecordType.MarketTrade), trade),
            _record(4, int(g.RecordType.OrderIntent), intent),
            _record(5, int(g.RecordType.OrderEvent), event),
            _record(6, int(g.RecordType.RiskRejected), risk),
            _record(7, int(g.RecordType.ControlCommand), command),
        ],
    )
    seen: list[str] = []
    report = project_segment(segment, apply=True, execute=seen.append)
    assert report.applied
    assert report.mapped == 7
    script = seen[0]
    assert "INSERT INTO trading.market_ticks" in script
    assert "INSERT INTO trading.orders" in script
    assert "INSERT INTO trading.order_events" in script
    assert "INSERT INTO trading.risk_events" in script
    assert "INSERT INTO trading.control_commands" in script
    assert "INSERT INTO trading.config_versions" in script
    assert "O''Hare" in script
    assert "2024-01-15 13:54:56.789000+00" in script
    assert "source_sequence <= EXCLUDED.source_sequence" in script
    assert "DO NOTHING" in script
    for row_sql in script.split("projection_checkpoint")[0].split("INSERT INTO trading."):
        if row_sql.startswith("market_ticks") or row_sql.startswith("orders"):
            assert "now()" not in row_sql


def test_fatal_segment_writes_nothing(tmp_path: Path) -> None:
    good = _record(1, int(g.RecordType.MarketQuote), _payload_quote())
    corrupt = bytearray(good)
    corrupt[g.RECORD_HEADER_SIZE + 2] ^= 0xFF
    segment = tmp_path / "bad.journal"
    segment.write_bytes(corrupt)
    before = hashlib.sha256(segment.read_bytes()).hexdigest()

    def _fail(sql: str) -> None:
        raise AssertionError(sql)

    report = project_segment(segment, apply=True, execute=_fail)
    assert not report.applied
    assert report.inspector_status.name == "FATAL"
    assert hashlib.sha256(segment.read_bytes()).hexdigest() == before


def test_database_outage_leaves_journal_unchanged(tmp_path: Path) -> None:
    segment = tmp_path / "live.journal"
    _write(segment, [_record(1, int(g.RecordType.MarketQuote), _payload_quote())])
    before = segment.read_bytes()

    def _down(sql: str) -> None:
        raise DatabaseUnavailable("connection refused")

    report = project_segment(segment, apply=True, execute=_down)
    assert report.database_unavailable
    assert not report.applied
    assert segment.read_bytes() == before
    assert report.journal_modified is False


def test_dry_run_does_not_execute(tmp_path: Path) -> None:
    segment = tmp_path / "dry.journal"
    _write(segment, [_record(1, int(g.RecordType.MarketQuote), _payload_quote())])

    def _fail(sql: str) -> None:
        raise AssertionError(sql)

    report = project_segment(segment, apply=False, execute=_fail)
    assert not report.applied
    assert not report.database_unavailable
    assert report.mapped == 1


def test_torn_tail_projects_prefix_without_truncating(tmp_path: Path) -> None:
    whole = _record(1, int(g.RecordType.MarketQuote), _payload_quote()) + _record(
        2, int(g.RecordType.MarketQuote), _payload_quote(20, 21)
    )
    torn = whole[:-5]
    segment = tmp_path / "torn.journal"
    segment.write_bytes(torn)
    seen: list[str] = []
    report = project_segment(segment, apply=True, execute=seen.append)
    assert report.alert_code == "AL-14"
    assert report.mapped == 1
    assert "', 1, '" in seen[0]
    assert "', 2, '" not in seen[0]
    assert segment.read_bytes() == torn


def test_secret_is_redacted() -> None:
    from tradelab_services.projector.project import _redact

    url = "postgres://trading:s3cret-value@127.0.0.1:5432/trading_dev"
    message = _redact(f"could not connect to {url}", url)
    assert "s3cret-value" not in message
    assert url not in message
