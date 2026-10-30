"""Live tick recorder — journal append only (no trading)."""

from __future__ import annotations

import asyncio
import json
import os
import time
from collections.abc import AsyncIterator, Iterator
from dataclasses import dataclass
from pathlib import Path

from tradelab_services.ingestion.delta_normalize import NormalizedQuote, NormalizedTrade, parse_public_message
from tradelab_services.ingestion.delta_ws import DEFAULT_DELTA_PUBLIC_WS_URL, iter_delta_public_messages
from tradelab_services.ingestion.health import (
    REASON_DISCONNECTED,
    REASON_SEQUENCE_GAP,
    REASON_STALE,
    FeedHealthTracker,
)
from tradelab_services.ingestion.instruments import RecordingInstrument, load_recording_instruments
from tradelab_services.ingestion.mock_feed import iter_mock_messages
from tradelab_services.ingestion.ws_scripted_feed import iter_scripted_ws_messages
from tradelab_services.journal import generated as g
from tradelab_services.journal.rotating_writer import DEFAULT_MAX_SEGMENT_BYTES, RotationPolicy, RotatingJournalWriter


@dataclass(frozen=True, slots=True)
class RecorderConfig:
    profile_name: str = "recording"
    journal_root: Path = Path("capture-root")
    instruments_file: Path = Path("tests/fixtures/ingestion/recording_instruments.toml")
    mock_feed_file: Path | None = None
    stale_after_s: float = 5.0
    max_segment_bytes: int = DEFAULT_MAX_SEGMENT_BYTES
    max_segment_age_s: float = 0.0
    ws_url: str = DEFAULT_DELTA_PUBLIC_WS_URL


def _mono_ns() -> int:
    return time.monotonic_ns()


def _wall_ns() -> int:
    return time.time_ns()


def _write_feed_state(
    writer: RotatingJournalWriter,
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


def _is_control_message(doc: dict[str, object]) -> bool:
    msg_type = doc.get("type")
    if not isinstance(msg_type, str):
        return True
    return msg_type in {"subscriptions", "success", "pong", "heartbeat", "subscription", "error"}


def _process_public_line(
    raw: str,
    *,
    writer: RotatingJournalWriter,
    symbol_map: dict[str, RecordingInstrument],
    trackers: dict[int, FeedHealthTracker],
) -> None:
    mono = _mono_ns()
    wall = _wall_ns()

    try:
        doc = json.loads(raw)
    except json.JSONDecodeError:
        return
    if not isinstance(doc, dict):
        return
    if _is_control_message(doc):
        return

    symbol = doc.get("symbol")
    if not isinstance(symbol, str):
        return
    instrument = symbol_map.get(symbol)
    if instrument is None:
        return

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

    normalized = parse_public_message(raw, instrument)
    if normalized is None:
        return

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


def _rotation_policy(config: RecorderConfig) -> RotationPolicy:
    age_ns = int(config.max_segment_age_s * 1_000_000_000) if config.max_segment_age_s > 0 else 0
    return RotationPolicy(max_segment_bytes=config.max_segment_bytes, max_segment_age_ns=age_ns)


def _begin_session(
    writer: RotatingJournalWriter, config: RecorderConfig, instruments: list[RecordingInstrument]
) -> dict[int, FeedHealthTracker]:
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
    return {
        inst.instrument_id: FeedHealthTracker(
            instrument_id=inst.instrument_id,
            stale_after_ns=int(config.stale_after_s * 1_000_000_000),
        )
        for inst in instruments
    }


def _end_session(writer: RotatingJournalWriter) -> None:
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
    writer.shutdown()


def _iter_mock_lines(config: RecorderConfig) -> Iterator[str]:
    if config.mock_feed_file is None:
        msg = "mock_feed_file is required for mock mode"
        raise ValueError(msg)
    yield from iter_mock_messages(config.mock_feed_file)


def run_recording_session(config: RecorderConfig) -> int:
    instruments = load_recording_instruments(config.instruments_file)
    if not instruments:
        return 2

    writer = RotatingJournalWriter(config.journal_root, _rotation_policy(config))
    trackers = _begin_session(writer, config, instruments)
    symbol_map = {inst.symbol: inst for inst in instruments}

    for line in _iter_mock_lines(config):
        _process_public_line(line, writer=writer, symbol_map=symbol_map, trackers=trackers)

    _end_session(writer)
    return 0


async def run_recording_session_async(config: RecorderConfig, message_source: AsyncIterator[str]) -> int:
    instruments = load_recording_instruments(config.instruments_file)
    if not instruments:
        return 2

    writer = RotatingJournalWriter(config.journal_root, _rotation_policy(config))
    trackers = _begin_session(writer, config, instruments)
    symbol_map = {inst.symbol: inst for inst in instruments}

    try:
        async for line in message_source:
            _process_public_line(line, writer=writer, symbol_map=symbol_map, trackers=trackers)
    finally:
        _end_session(writer)
    return 0


async def run_live_ws_session(config: RecorderConfig) -> int:
    instruments = load_recording_instruments(config.instruments_file)
    if not instruments:
        return 2

    symbols = [inst.symbol for inst in instruments]
    running = True
    writer = RotatingJournalWriter(config.journal_root, _rotation_policy(config))
    trackers = _begin_session(writer, config, instruments)
    symbol_map = {inst.symbol: inst for inst in instruments}

    def should_run() -> bool:
        return running

    def on_disconnect() -> None:
        mono = _mono_ns()
        wall = _wall_ns()
        for inst in instruments:
            _write_feed_state(
                writer,
                instrument_id=inst.instrument_id,
                state=g.FeedHealthState.Stale,
                reason_code=REASON_DISCONNECTED,
                mono_ns=mono,
                wall_ns=wall,
            )

    source = iter_delta_public_messages(
        ws_url=config.ws_url,
        symbols=symbols,
        should_run=should_run,
        on_disconnect=on_disconnect,
    )
    try:
        async for line in source:
            _process_public_line(line, writer=writer, symbol_map=symbol_map, trackers=trackers)
    finally:
        running = False
        _end_session(writer)
    return 0


async def run_mock_ws_session(config: RecorderConfig) -> int:
    if config.mock_feed_file is None:
        msg = "mock_feed_file is required for mock-ws mode"
        raise ValueError(msg)
    return await run_recording_session_async(config, iter_scripted_ws_messages(config.mock_feed_file))


def default_journal_root() -> Path:
    repo_root = Path(__file__).resolve().parents[4]
    journal_root = Path(os.environ.get("TRADELAB_JOURNAL_ROOT", str(repo_root / ".handoff" / "journals")))
    journal_root.mkdir(parents=True, exist_ok=True)
    session_root = journal_root / f"capture-{int(time.time())}"
    session_root.mkdir(parents=True, exist_ok=True)
    return session_root
