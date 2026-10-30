"""Build journal data-quality reports from lenient scans."""

from __future__ import annotations

import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

_INSPECT = Path(__file__).resolve().parents[1] / "journal_inspect"
if str(_INSPECT) not in sys.path:
    sys.path.insert(0, str(_INSPECT))

from segment import InspectStatus, ParsedRecord  # noqa: E402

from _generated import load_generated  # noqa: E402

from journal_dq_report.scan import (  # noqa: E402
    LenientScanResult,
    scan_segment_lenient,
    strict_structural_check,
)

g = load_generated()

FEED_STALE = int(g.FeedHealthState.Stale)
RT_MARKET_QUOTE = int(g.RecordType.MarketQuote)
RT_MARKET_TRADE = int(g.RecordType.MarketTrade)
RT_FEED_STATE = int(g.RecordType.FeedState)


@dataclass(frozen=True, slots=True)
class StalePeriod:
    instrument_id: int
    start_wall_ns: int
    end_wall_ns: int
    source: str


@dataclass
class InstrumentStats:
    instrument_id: int
    quote_count: int = 0
    trade_count: int = 0
    feed_state_count: int = 0
    first_wall_ns: int | None = None
    last_wall_ns: int | None = None
    max_inter_event_gap_ns: int = 0
    stale_gap_count: int = 0


@dataclass
class DataQualityReport:
    segment_path: str
    structural_status: str
    structural_message: str
    alert_code: str | None
    error_offset: int | None
    record_count: int
    sequence_gap_count: int
    sequence_gaps: list[dict[str, int]]
    stale_period_count: int
    stale_periods: list[dict[str, int | str]]
    stale_threshold_ns: int
    instruments: list[dict[str, int | None]]
    inspector_agrees_ok: bool

    def to_text(self) -> str:
        lines = [
            f"segment: {self.segment_path}",
            f"structural: {self.structural_status} — {self.structural_message}",
            f"records: {self.record_count}",
            f"sequence_gaps: {self.sequence_gap_count}",
            f"stale_periods (threshold_ns={self.stale_threshold_ns}): {self.stale_period_count}",
        ]
        if self.alert_code:
            lines.append(f"alert: {self.alert_code}")
        for inst in self.instruments:
            lines.append(
                "instrument "
                f"{inst['instrument_id']}: quotes={inst['quote_count']} trades={inst['trade_count']} "
                f"max_gap_ns={inst['max_inter_event_gap_ns']} stale_gaps={inst['stale_gap_count']}"
            )
        return "\n".join(lines) + "\n"

    def to_json(self) -> str:
        return json.dumps(asdict(self), indent=2, sort_keys=True) + "\n"


def _touch(stats: InstrumentStats, wall_ns: int) -> None:
    if stats.first_wall_ns is None or wall_ns < stats.first_wall_ns:
        stats.first_wall_ns = wall_ns
    if stats.last_wall_ns is None or wall_ns > stats.last_wall_ns:
        stats.last_wall_ns = wall_ns


def analyze_records(
    records: list[ParsedRecord],
    *,
    stale_threshold_ns: int,
) -> tuple[dict[int, InstrumentStats], list[StalePeriod]]:
    per_instrument: dict[int, InstrumentStats] = {}
    market_events: dict[int, list[tuple[int, int]]] = {}
    stale_periods: list[StalePeriod] = []

    def stats_for(iid: int) -> InstrumentStats:
        if iid not in per_instrument:
            per_instrument[iid] = InstrumentStats(instrument_id=iid)
            market_events[iid] = []
        return per_instrument[iid]

    for rec in records:
        wall = rec.header.wall_ns
        rtype = rec.header.record_type
        if rtype == RT_MARKET_QUOTE:
            payload = g.decode_market_quote(rec.payload)
            st = stats_for(payload.instrument_id)
            st.quote_count += 1
            _touch(st, wall)
            market_events[payload.instrument_id].append((wall, rtype))
        elif rtype == RT_MARKET_TRADE:
            payload = g.decode_market_trade(rec.payload)
            st = stats_for(payload.instrument_id)
            st.trade_count += 1
            _touch(st, wall)
            market_events[payload.instrument_id].append((wall, rtype))
        elif rtype == RT_FEED_STATE:
            payload = g.decode_feed_state(rec.payload)
            st = stats_for(payload.instrument_id)
            st.feed_state_count += 1
            _touch(st, wall)
            if int(payload.state) == FEED_STALE:
                stale_periods.append(
                    StalePeriod(
                        instrument_id=payload.instrument_id,
                        start_wall_ns=wall,
                        end_wall_ns=wall,
                        source="feed_state",
                    )
                )

    for iid, evs in market_events.items():
        evs.sort(key=lambda x: x[0])
        st = stats_for(iid)
        for idx in range(1, len(evs)):
            prev_wall = evs[idx - 1][0]
            wall = evs[idx][0]
            gap = wall - prev_wall
            if gap > st.max_inter_event_gap_ns:
                st.max_inter_event_gap_ns = gap
            if gap > stale_threshold_ns:
                st.stale_gap_count += 1
                stale_periods.append(
                    StalePeriod(
                        instrument_id=iid,
                        start_wall_ns=prev_wall,
                        end_wall_ns=wall,
                        source="market_event_gap",
                    )
                )

    return per_instrument, stale_periods


def build_report(
    segment_path: Path,
    data: bytes,
    *,
    stale_threshold_ns: int,
) -> DataQualityReport:
    scan: LenientScanResult = scan_segment_lenient(data)
    strict = strict_structural_check(data)
    per_instrument, stale_periods = analyze_records(
        scan.records,
        stale_threshold_ns=stale_threshold_ns,
    )

    structural_status = scan.structural.status.name
    if strict.status != InspectStatus.OK and scan.structural.status == InspectStatus.OK:
        structural_status = strict.status.name
        structural_message = strict.message
        alert_code = strict.alert_code
        error_offset = strict.error_offset
    else:
        structural_message = scan.structural.message
        alert_code = scan.structural.alert_code
        error_offset = scan.structural.error_offset

    instruments_out = [
        {
            "instrument_id": st.instrument_id,
            "quote_count": st.quote_count,
            "trade_count": st.trade_count,
            "feed_state_count": st.feed_state_count,
            "first_wall_ns": st.first_wall_ns,
            "last_wall_ns": st.last_wall_ns,
            "max_inter_event_gap_ns": st.max_inter_event_gap_ns,
            "stale_gap_count": st.stale_gap_count,
        }
        for st in sorted(per_instrument.values(), key=lambda s: s.instrument_id)
    ]

    return DataQualityReport(
        segment_path=str(segment_path),
        structural_status=structural_status,
        structural_message=structural_message,
        alert_code=alert_code,
        error_offset=error_offset,
        record_count=len(scan.records),
        sequence_gap_count=len(scan.sequence_gaps),
        sequence_gaps=[
            {
                "expected_sequence": gap.expected_sequence,
                "actual_sequence": gap.actual_sequence,
                "offset": gap.offset,
            }
            for gap in scan.sequence_gaps
        ],
        stale_period_count=len(stale_periods),
        stale_periods=[
            {
                "instrument_id": p.instrument_id,
                "start_wall_ns": p.start_wall_ns,
                "end_wall_ns": p.end_wall_ns,
                "source": p.source,
            }
            for p in stale_periods
        ],
        stale_threshold_ns=stale_threshold_ns,
        instruments=instruments_out,
        inspector_agrees_ok=strict.status == InspectStatus.OK and not scan.sequence_gaps,
    )
