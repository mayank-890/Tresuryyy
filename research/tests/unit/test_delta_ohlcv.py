"""T-0101: Delta OHLCV ingest, Parquet, and quality (fixtures only — no network)."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from tradelab.cli.ohlcv_backfill import main as backfill_main
from tradelab.data.adjust.futures_roll import apply_futures_roll_adjustment
from tradelab.data.ingest.delta_rest import (
    OhlcvBar,
    build_candles_url,
    fetch_candles_range,
    parse_candles_response,
    resolution_to_seconds,
)
from tradelab.data.quality.checks import detect_gaps, flag_outliers, run_quality_checks
from tradelab.data.schema import SOURCE_DELTA_REST, VENUE_DELTA_PUBLIC
from tradelab.data.store.parquet_io import merge_write_bars_parquet, read_bars_parquet
from tradelab.data.store.paths import ohlcv_parquet_path

REPO_ROOT = Path(__file__).resolve().parents[3]
DELTA_FIXTURE = REPO_ROOT / "tests" / "fixtures" / "delta" / "candles_btcusd_1m.json"


class FixtureClient:
    def __init__(self, payload: object) -> None:
        self._payload = payload
        self.urls: list[str] = []

    def get_json(self, url: str) -> object:
        self.urls.append(url)
        return self._payload


def _load_fixture() -> object:
    return json.loads(DELTA_FIXTURE.read_text(encoding="utf-8"))


def test_parse_candles_fixture() -> None:
    bars = parse_candles_response(_load_fixture(), symbol="BTCUSD", resolution="1m")
    assert len(bars) == 3
    assert bars[0].open_time_ns == 1704067200 * 1_000_000_000
    assert bars[0].venue == VENUE_DELTA_PUBLIC
    assert bars[0].source == SOURCE_DELTA_REST


def test_build_candles_url() -> None:
    url = build_candles_url(
        base_url="https://api.delta.exchange",
        symbol="BTCUSD",
        resolution="1m",
        start_s=1,
        end_s=60,
    )
    assert "symbol=BTCUSD" in url
    assert "resolution=1m" in url
    assert url.startswith("https://api.delta.exchange/v2/history/candles?")


def test_detect_gap_in_fixture_series() -> None:
    bars = parse_candles_response(_load_fixture(), symbol="BTCUSD", resolution="1m")
    gaps = detect_gaps(bars)
    assert len(gaps) == 1
    assert gaps[0].missing_open_time_ns == 1704067320 * 1_000_000_000


def test_outlier_flags_structural() -> None:
    bad = OhlcvBar(
        symbol="X",
        venue=VENUE_DELTA_PUBLIC,
        resolution="1m",
        open_time_ns=1,
        open=10.0,
        high=9.0,
        low=11.0,
        close=10.0,
        volume=-1.0,
        source=SOURCE_DELTA_REST,
    )
    flags = flag_outliers([bad], max_abs_return=1.0)
    reasons = {f.reason for f in flags}
    assert "high_below_low" in reasons
    assert "negative_volume" in reasons


def test_parquet_round_trip(tmp_path: Path) -> None:
    bars = parse_candles_response(_load_fixture(), symbol="BTCUSD", resolution="1m")
    path = ohlcv_parquet_path(tmp_path, symbol="BTCUSD", resolution="1m")
    merge_write_bars_parquet(path, bars)
    loaded = read_bars_parquet(path)
    assert len(loaded) == 3
    assert loaded[1].close == pytest.approx(101.0)


def test_fetch_range_uses_client_without_network() -> None:
    client = FixtureClient(_load_fixture())
    bars = fetch_candles_range(
        client,
        base_url="https://example.test",
        symbol="BTCUSD",
        resolution="1m",
        start_s=1704067200,
        end_s=1704067500,
        max_bars_per_request=500,
    )
    assert len(bars) == 3
    assert len(client.urls) >= 1


def test_backfill_cli_dry_run(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    client = FixtureClient(_load_fixture())
    monkeypatch.setattr(
        "tradelab.cli.ohlcv_backfill.UrllibJsonClient",
        lambda: client,
    )
    rc = backfill_main(
        [
            "--symbol",
            "BTCUSD",
            "--resolution",
            "1m",
            "--start",
            "2024-01-01",
            "--end",
            "2024-01-02",
            "--dry-run",
        ]
    )
    assert rc == 0
    err = capsys.readouterr().err
    assert "gaps=1" in err


def test_futures_roll_stub_is_identity() -> None:
    bars = parse_candles_response(_load_fixture(), symbol="BTCUSD", resolution="1m")
    assert apply_futures_roll_adjustment(bars) == bars


def test_resolution_to_seconds_known() -> None:
    assert resolution_to_seconds("1m") == 60
    assert resolution_to_seconds("1h") == 3600


def test_quality_report_counts() -> None:
    bars = parse_candles_response(_load_fixture(), symbol="BTCUSD", resolution="1m")
    report = run_quality_checks(bars)
    assert report.gap_count == 1
    assert report.outlier_count == 0
