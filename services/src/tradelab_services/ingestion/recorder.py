"""Live tick recorder — journal append only (no trading)."""

from __future__ import annotations

import json
import os
import time
import uuid
from dataclasses import dataclass
from pathlib import Path

from tradelab_services.ingestion.delta_normalize import NormalizedQuote, NormalizedTrade, parse_public_message
from tradelab_services.ingestion.health import (
    REASON_SEQUENCE_GAP,
    REASON_STALE,
    FeedHealthTracker,
)
from tradelab_services.ingestion.instruments import RecordingInstrument, load_recording_instruments
from tradelab_services.ingestion.mock_feed import iter_mock_messages
from tradelab_services.journal import generated as g
from tradelab_services.journal.writer import JournalWriter


@dataclass(frozen=True, slots=True)
class RecorderConfig:
    profile_name: str = "recording"
    journal_path: Path = Path("capture.journal")
    instruments_file: Path = Path("tests/fixtures/ingestion/recording_instruments.toml")
    mock_feed_file: Path | None = None
    stale_after_s: float = 5.0


def _mono_ns() -> int:
    return time.monotonic_ns()


def _wall_ns() -> int:
    return time.time_ns()


def _write_feed_state(
    writer: JournalWriter,
    *,
    instrument_id: int,
    state: g.FeedHealthState,
    reason_code: int,
    mono_ns: int,
    wall_ns: int,
) -> None:
    payload = g.encode_feed_state(
        g.FeedStatePayload(
            instrument_id=instrument_id,
            state=int(state),
            reason_code=reason_code,
        )
    )
    writer.append(
        g.RecordType.FeedState,
        payload,
        monotonic_ns=mono_ns,
        wall_ns=wall_ns,
    )


def run_recording_session(config: RecorderConfig) -> int:
    instruments = load_recording_instruments(config.instruments_file)
    if not instruments:
        return 2

    writer = JournalWriter(config.journal_path)
    writer.open()

    writer.append(
        g.RecordType.SessionStart,
        g.encode_session_start(
            g.SessionStartPayload(
                profile_name=config.profile_name,
                config_hash=bytes(32),
                engine_build_hash=bytes(32),
                client_id_prefix="REC1",
            )
        ),
        monotonic_ns=_mono_ns(),
        wall_ns=_wall_ns(),
    )

    trackers = {
        inst.instrument_id: FeedHealthTracker(
            instrument_id=inst.instrument_id,
            stale_after_ns=int(config.stale_after_s * 1_000_000_000),
        )
        for inst in instruments
    }
    symbol_map = {inst.symbol: inst for inst in instruments}

    if config.mock_feed_file is None:
        msg = "mock_feed_file is required for fixture runs; TODO(operator): Delta public WS/REST"
        raise ValueError(msg)

    for line in iter_mock_messages(config.mock_feed_file):
        mono = _mono_ns()
        wall = _wall_ns()

        try:
            doc = json.loads(line)
        except json.JSONDecodeError:
            continue

        symbol = doc.get("symbol")
        if not isinstance(symbol, str):
            continue
        instrument = symbol_map.get(symbol)
        if instrument is None:
            continue

        stale = trackers[instrument.instrument_id].stale_reason(mono)
        if stale is not None:
            _write_feed_state(
                writer,
                instrument_id=instrument.instrument_id,
                state=g.FeedHealthState.Stale,
                reason_code=REASON_STALE,
                mono_ns=mono,
                wall_ns=wall,
            )

        seq_field = doc.get("seq")
        if isinstance(seq_field, int):
            gap = trackers[instrument.instrument_id].observe_sequence(seq_field)
            if gap is not None:
                _write_feed_state(
                    writer,
                    instrument_id=instrument.instrument_id,
                    state=g.FeedHealthState.Live,
                    reason_code=REASON_SEQUENCE_GAP,
                    mono_ns=mono,
                    wall_ns=wall,
                )

        normalized = parse_public_message(line, instrument)
        if normalized is None:
            continue

        trackers[instrument.instrument_id].touch(mono)

        if isinstance(normalized, NormalizedQuote):
            writer.append(
                g.RecordType.MarketQuote,
                g.encode_market_quote(normalized.payload),
                monotonic_ns=mono,
                wall_ns=wall,
            )
        elif isinstance(normalized, NormalizedTrade):
            writer.append(
                g.RecordType.MarketTrade,
                g.encode_market_trade(normalized.payload),
                monotonic_ns=mono,
                wall_ns=wall,
            )

    writer.append(
        g.RecordType.SessionEnd,
        g.encode_session_end(
            g.SessionEndPayload(
                g.SessionEndReason.NormalShutdown,
                g.EngineRunState.Running,
            )
        ),
        monotonic_ns=_mono_ns(),
        wall_ns=_wall_ns(),
    )
    writer.flush()
    writer.close()
    return 0


def default_journal_path() -> Path:
    repo_root = Path(__file__).resolve().parents[4]
    journal_root = Path(os.environ.get("TRADELAB_JOURNAL_ROOT", str(repo_root / ".handoff" / "journals")))
    journal_root.mkdir(parents=True, exist_ok=True)
    return journal_root / f"capture-{uuid.uuid4().hex[:8]}.journal"
