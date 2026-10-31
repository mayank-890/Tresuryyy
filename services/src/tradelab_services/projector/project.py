"""Read v1 journal segments and insert idempotent projection rows.

PostgreSQL is reached through ``psql`` (the client already used by ``just test-db``).
No new driver is added. A database failure leaves the journal file unchanged.
``event_time`` comes from the record header ``wall_ns`` (DESIGN §11). ``now()`` is
used only for ``projection_checkpoint.updated_at``, which the migration defines
as checkpoint metadata.

This stub re-reads the segment on every run. Missing sequences are reported and
are not synthesized. Records are written in sequence order. A FATAL scan
(CRC, bad magic, sequence gap in the file) writes nothing.
"""

from __future__ import annotations

import os
import platform
import shutil
import subprocess
import sys
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from datetime import UTC, datetime
from enum import IntEnum
from pathlib import Path

_SERVICES_SRC = Path(__file__).resolve().parents[2]
_REPO = _SERVICES_SRC.parents[1]
_TOOLS = _REPO / "tools"
for _entry in (str(_SERVICES_SRC), str(_TOOLS), str(_TOOLS / "journal_inspect")):
    if _entry not in sys.path:
        sys.path.insert(0, _entry)

from journal_retention.footer import FOOTER_SIZE, decode_footer  # noqa: E402
from segment import InspectStatus, ParsedRecord, inspect_segment  # noqa: E402

from tradelab_services.journal import generated as g  # noqa: E402

PROJECTOR_ID = "journal-stub"
_SEGMENT_NAME_OK = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-")


class DatabaseUnavailable(Exception):
    """PostgreSQL could not be written. The journal file is not modified."""


@dataclass(frozen=True)
class SequenceGap:
    expected_sequence: int
    actual_sequence: int


@dataclass(frozen=True)
class ProjectionPlan:
    statements: tuple[str, ...]
    gaps: tuple[SequenceGap, ...]
    mapped: int
    skipped: int
    max_sequence: int | None
    refused: str | None = None


@dataclass(frozen=True)
class ProjectionReport:
    inspector_status: InspectStatus
    applied: bool
    database_unavailable: bool
    message: str
    alert_code: str | None
    mapped: int
    skipped: int
    gaps: tuple[SequenceGap, ...]
    journal_modified: bool = False


def event_time_literal(wall_ns: int) -> str:
    """Format ``wall_ns`` as a UTC timestamptz literal. Sub-microsecond ns are dropped."""
    if wall_ns < 0:
        msg = "wall_ns must be >= 0"
        raise ValueError(msg)
    seconds, rem_ns = divmod(wall_ns, 1_000_000_000)
    micros = rem_ns // 1000
    stamp = datetime.fromtimestamp(seconds, tz=UTC)
    return f"{stamp.strftime('%Y-%m-%d %H:%M:%S')}.{micros:06d}+00"


def sql_int(value: int) -> str:
    if isinstance(value, bool) or not isinstance(value, int):
        msg = "SQL integer required"
        raise TypeError(msg)
    return str(value)


def sql_text(value: str) -> str:
    if "\x00" in value:
        msg = "NUL in SQL text"
        raise ValueError(msg)
    return "'" + value.replace("'", "''") + "'"


def _enum_name(enum_cls: type[IntEnum], value: int) -> str:
    for member in enum_cls:
        if int(member) == value:
            return member.name
    return str(value)


def _segment_name(path: Path) -> str:
    name = path.name
    if not name or len(name) > 200 or any(ch not in _SEGMENT_NAME_OK for ch in name):
        msg = f"unsafe segment name {name!r}"
        raise ValueError(msg)
    return name


def _body(data: bytes) -> bytes:
    if len(data) >= FOOTER_SIZE and decode_footer(data[-FOOTER_SIZE:]) is not None:
        return data[:-FOOTER_SIZE]
    return data


def _sorted(records: Sequence[ParsedRecord]) -> list[ParsedRecord]:
    return sorted(enumerate(records), key=lambda item: (item[1].header.sequence, item[0]))


def sequence_gaps(records: Sequence[ParsedRecord]) -> tuple[SequenceGap, ...]:
    ordered = [rec for _, rec in _sorted(records)]
    gaps: list[SequenceGap] = []
    previous: int | None = None
    for rec in ordered:
        sequence = rec.header.sequence
        if previous is not None and sequence != previous + 1:
            gaps.append(SequenceGap(previous + 1, sequence))
        previous = sequence
    return tuple(gaps)


def _insert_market(
    segment: str,
    sequence: int,
    event_time: str,
    instrument_id: int,
    *,
    bid_ticks: int | None,
    ask_ticks: int | None,
    last_ticks: int | None,
    volume_lots: int | None,
) -> str:
    def nullable(value: int | None) -> str:
        return "NULL" if value is None else sql_int(value)

    return (
        "INSERT INTO trading.market_ticks ("
        "source_segment, source_sequence, event_time, instrument_id, "
        "bid_ticks, ask_ticks, last_ticks, volume_lots) VALUES ("
        f"{sql_text(segment)}, {sql_int(sequence)}, "
        f"{sql_text(event_time)}::timestamptz, {sql_int(instrument_id)}, "
        f"{nullable(bid_ticks)}, {nullable(ask_ticks)}, "
        f"{nullable(last_ticks)}, {nullable(volume_lots)}) "
        "ON CONFLICT (source_segment, source_sequence, event_time) DO NOTHING;"
    )


def _statement_for(segment: str, rec: ParsedRecord) -> str | None:
    header = rec.header
    event_time = event_time_literal(header.wall_ns)
    sequence = header.sequence
    record_type = header.record_type
    payload = rec.payload

    if record_type == int(g.RecordType.MarketQuote):
        quote = g.decode_market_quote(payload)
        return _insert_market(
            segment,
            sequence,
            event_time,
            quote.instrument_id,
            bid_ticks=quote.bid_ticks,
            ask_ticks=quote.ask_ticks,
            last_ticks=None,
            volume_lots=None,
        )
    if record_type == int(g.RecordType.MarketTrade):
        trade = g.decode_market_trade(payload)
        return _insert_market(
            segment,
            sequence,
            event_time,
            trade.instrument_id,
            bid_ticks=None,
            ask_ticks=None,
            last_ticks=trade.price_ticks,
            volume_lots=trade.size_lots,
        )
    if record_type == int(g.RecordType.OrderIntent):
        intent = g.decode_order_intent(payload)
        return (
            "INSERT INTO trading.orders ("
            "client_order_id, source_segment, source_sequence, strategy_id, instrument_id, "
            "side, status, qty_lots, limit_price_ticks, updated_at) VALUES ("
            f"{sql_text(intent.client_id)}, {sql_text(segment)}, {sql_int(sequence)}, "
            f"{sql_int(intent.strategy_id)}, {sql_int(intent.instrument_id)}, "
            f"{sql_int(intent.side)}, {sql_int(int(g.OrderState.IntentJournaled))}, "
            f"{sql_int(intent.qty_lots)}, {sql_int(intent.price_ticks)}, "
            f"{sql_text(event_time)}::timestamptz) "
            "ON CONFLICT (client_order_id) DO UPDATE SET "
            "source_segment = EXCLUDED.source_segment, "
            "source_sequence = EXCLUDED.source_sequence, "
            "strategy_id = EXCLUDED.strategy_id, "
            "instrument_id = EXCLUDED.instrument_id, "
            "side = EXCLUDED.side, "
            "status = EXCLUDED.status, "
            "qty_lots = EXCLUDED.qty_lots, "
            "limit_price_ticks = EXCLUDED.limit_price_ticks, "
            "updated_at = EXCLUDED.updated_at "
            "WHERE trading.orders.source_sequence <= EXCLUDED.source_sequence;"
        )
    if record_type == int(g.RecordType.OrderEvent):
        event = g.decode_order_event(payload)
        order_events = (
            "INSERT INTO trading.order_events ("
            "source_segment, source_sequence, event_time, client_order_id, "
            "from_status, to_status, reason_code) VALUES ("
            f"{sql_text(segment)}, {sql_int(sequence)}, {sql_text(event_time)}::timestamptz, "
            f"{sql_text(event.client_id)}, {sql_int(event.from_state)}, "
            f"{sql_int(event.to_state)}, {sql_int(event.reason_code)}) "
            "ON CONFLICT (source_segment, source_sequence, event_time) DO NOTHING;"
        )
        status_update = (
            "UPDATE trading.orders SET "
            f"status = {sql_int(event.to_state)}, "
            f"source_segment = {sql_text(segment)}, "
            f"source_sequence = {sql_int(sequence)}, "
            f"updated_at = {sql_text(event_time)}::timestamptz "
            f"WHERE client_order_id = {sql_text(event.client_id)} "
            f"AND source_sequence <= {sql_int(sequence)};"
        )
        return order_events + "\n" + status_update
    if record_type == int(g.RecordType.RiskRejected):
        risk = g.decode_risk_rejected(payload)
        detail = (
            f"client_id={risk.client_id} limit_id={risk.limit_id} "
            f"observed={risk.observed} threshold={risk.threshold}"
        )
        return (
            "INSERT INTO trading.risk_events ("
            "source_segment, source_sequence, event_time, event_type, detail) VALUES ("
            f"{sql_text(segment)}, {sql_int(sequence)}, {sql_text(event_time)}::timestamptz, "
            f"{sql_text('RiskRejected')}, {sql_text(detail)}) "
            "ON CONFLICT (source_segment, source_sequence, event_time) DO NOTHING;"
        )
    if record_type == int(g.RecordType.ControlCommand):
        command = g.decode_control_command(payload)
        payload_text = (
            f"scope={_enum_name(g.ControlScope, command.scope)} "
            f"outcome={_enum_name(g.CommandOutcome, command.outcome)}"
        )
        return (
            "INSERT INTO trading.control_commands ("
            "source_segment, source_sequence, event_time, operator_id, command_type, payload) "
            "VALUES ("
            f"{sql_text(segment)}, {sql_int(sequence)}, {sql_text(event_time)}::timestamptz, "
            f"{sql_text(command.operator_ref)}, "
            f"{sql_text(_enum_name(g.ControlCommandKind, command.command))}, "
            f"{sql_text(payload_text)}) "
            "ON CONFLICT (source_segment, source_sequence) DO NOTHING;"
        )
    if record_type == int(g.RecordType.SessionStart):
        start = g.decode_session_start(payload)
        return (
            "INSERT INTO trading.config_versions ("
            "source_segment, source_sequence, event_time, content_hash, profile_name) VALUES ("
            f"{sql_text(segment)}, {sql_int(sequence)}, {sql_text(event_time)}::timestamptz, "
            f"{sql_text(start.config_hash.hex())}, {sql_text(start.profile_name)}) "
            "ON CONFLICT DO NOTHING;"
        )
    return None


def plan_records(segment: str, records: Sequence[ParsedRecord]) -> ProjectionPlan:
    """Sort records, report holes, and build idempotent SQL. Does not touch a database."""
    if not segment or any(ch not in _SEGMENT_NAME_OK for ch in segment):
        return ProjectionPlan((), (), 0, 0, None, refused=f"unsafe segment name {segment!r}")
    ordered = [rec for _, rec in _sorted(records)]
    seen: set[int] = set()
    for rec in ordered:
        if rec.header.sequence in seen:
            return ProjectionPlan(
                (),
                (),
                0,
                0,
                None,
                refused=f"duplicate sequence {rec.header.sequence}",
            )
        seen.add(rec.header.sequence)
    gaps = sequence_gaps(ordered)
    statements: list[str] = []
    mapped = 0
    skipped = 0
    for rec in ordered:
        statement = _statement_for(segment, rec)
        if statement is None:
            skipped += 1
            continue
        statements.append(statement)
        mapped += 1
    max_sequence = ordered[-1].header.sequence if ordered else None
    if mapped and max_sequence is not None:
        statements.append(
            "INSERT INTO trading.projection_checkpoint ("
            "projector_id, last_source_segment, last_source_sequence, updated_at) VALUES ("
            f"{sql_text(PROJECTOR_ID)}, {sql_text(segment)}, {sql_int(max_sequence)}, now()) "
            "ON CONFLICT (projector_id) DO UPDATE SET "
            "last_source_segment = CASE "
            "WHEN EXCLUDED.last_source_sequence >= "
            "trading.projection_checkpoint.last_source_sequence "
            "THEN EXCLUDED.last_source_segment "
            "ELSE trading.projection_checkpoint.last_source_segment END, "
            "last_source_sequence = GREATEST("
            "trading.projection_checkpoint.last_source_sequence, EXCLUDED.last_source_sequence), "
            "updated_at = now();"
        )
    return ProjectionPlan(tuple(statements), gaps, mapped, skipped, max_sequence)


def sql_script(plan: ProjectionPlan) -> str:
    if plan.refused or not plan.statements:
        return ""
    return "BEGIN;\n" + "\n".join(plan.statements) + "\nCOMMIT;\n"


def _redact(text: str, database_url: str) -> str:
    redacted = text
    if database_url:
        redacted = redacted.replace(database_url, "***")
        password = _password(database_url)
        if password:
            redacted = redacted.replace(password, "***")
    return redacted


def _password(database_url: str) -> str:
    # postgres://user:password@host/db
    try:
        after_scheme = database_url.split("://", 1)[1]
        userinfo = after_scheme.split("@", 1)[0]
    except IndexError:
        return ""
    if ":" not in userinfo:
        return ""
    return userinfo.split(":", 1)[1]


def _psql_command(database_url: str) -> list[str]:
    flags = ["-v", "ON_ERROR_STOP=1", "-X", "-q", "--no-password", "-f", "-"]
    if shutil.which("psql"):
        return ["psql", database_url, *flags]
    url = database_url
    if platform.system() == "Darwin":
        url = url.replace("127.0.0.1", "host.docker.internal")
    return ["docker", "run", "--rm", "-i", "postgres:16-bookworm", "psql", url, *flags]


def run_psql(database_url: str, sql: str, *, tuples_only: bool = False) -> str:
    """Execute SQL. Raise DatabaseUnavailable without including the URL in the message."""
    if not database_url or "://" not in database_url:
        raise DatabaseUnavailable("DATABASE_URL is not set")
    command = _psql_command(database_url)
    if tuples_only:
        command = [*command[:-2], "-tA", *command[-2:]]
    try:
        proc = subprocess.run(
            command,
            input=sql,
            text=True,
            capture_output=True,
            timeout=60,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise DatabaseUnavailable("psql failed") from exc
    if proc.returncode != 0:
        detail = _redact(proc.stderr.strip(), database_url) or "psql failed"
        raise DatabaseUnavailable(detail)
    return proc.stdout


def project_segment(
    path: Path,
    *,
    apply: bool,
    execute: Callable[[str], None] | None = None,
    database_url: str | None = None,
) -> ProjectionReport:
    """Project one segment. Reads the file. Does not truncate or rewrite it."""
    before = path.read_bytes()
    inspected = inspect_segment(_body(before))
    alert = inspected.alert_code if inspected.status is InspectStatus.TORN_TAIL else None

    def unchanged(report: ProjectionReport) -> ProjectionReport:
        if path.read_bytes() != before:
            msg = "projector must not modify the journal"
            raise RuntimeError(msg)
        return report

    if inspected.status is InspectStatus.FATAL:
        return unchanged(
            ProjectionReport(
                inspector_status=inspected.status,
                applied=False,
                database_unavailable=False,
                message=inspected.message,
                alert_code=None,
                mapped=0,
                skipped=0,
                gaps=(),
            )
        )

    try:
        segment = _segment_name(path)
    except ValueError as exc:
        return unchanged(
            ProjectionReport(
                inspector_status=inspected.status,
                applied=False,
                database_unavailable=False,
                message=str(exc),
                alert_code=alert,
                mapped=0,
                skipped=0,
                gaps=(),
            )
        )

    plan = plan_records(segment, inspected.records)
    if plan.refused:
        return unchanged(
            ProjectionReport(
                inspector_status=inspected.status,
                applied=False,
                database_unavailable=False,
                message=plan.refused,
                alert_code=alert,
                mapped=0,
                skipped=0,
                gaps=plan.gaps,
            )
        )

    script = sql_script(plan)
    if not apply or not script:
        message = inspected.message
        if plan.gaps:
            message = f"{message}; sequence gaps (not synthesized): {_gap_text(plan.gaps)}"
        return unchanged(
            ProjectionReport(
                inspector_status=inspected.status,
                applied=False,
                database_unavailable=False,
                message=message,
                alert_code=alert,
                mapped=plan.mapped,
                skipped=plan.skipped,
                gaps=plan.gaps,
            )
        )

    def _run(sql: str) -> None:
        if execute is not None:
            execute(sql)
            return
        url = database_url if database_url is not None else os.environ.get("DATABASE_URL", "")
        run_psql(url, sql)

    try:
        _run(script)
    except DatabaseUnavailable as exc:
        return unchanged(
            ProjectionReport(
                inspector_status=inspected.status,
                applied=False,
                database_unavailable=True,
                message=str(exc),
                alert_code=alert,
                mapped=plan.mapped,
                skipped=plan.skipped,
                gaps=plan.gaps,
            )
        )
    message = f"projected {plan.mapped} record(s)"
    if plan.gaps:
        message = f"{message}; sequence gaps (not synthesized): {_gap_text(plan.gaps)}"
    return unchanged(
        ProjectionReport(
            inspector_status=inspected.status,
            applied=True,
            database_unavailable=False,
            message=message,
            alert_code=alert,
            mapped=plan.mapped,
            skipped=plan.skipped,
            gaps=plan.gaps,
        )
    )


def _gap_text(gaps: Sequence[SequenceGap]) -> str:
    return ", ".join(f"{gap.expected_sequence}->{gap.actual_sequence}" for gap in gaps)


def format_report(report: ProjectionReport) -> str:
    gap_text = _gap_text(report.gaps)
    lines = [
        f"inspector_status={report.inspector_status.name}",
        f"applied={str(report.applied).lower()}",
        f"database_unavailable={str(report.database_unavailable).lower()}",
        f"mapped={report.mapped}",
        f"skipped={report.skipped}",
        f"gaps={gap_text}",
        f"journal_modified={str(report.journal_modified).lower()}",
        f"message={report.message}",
    ]
    if report.alert_code is not None:
        lines.insert(1, f"alert={report.alert_code}")
    return "\n".join(lines) + "\n"


def exit_code(report: ProjectionReport) -> int:
    if report.database_unavailable:
        return 3
    if report.inspector_status is InspectStatus.FATAL or report.message.startswith("unsafe"):
        return 2
    if report.message.startswith("duplicate sequence"):
        return 2
    return 0
