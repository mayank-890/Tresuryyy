"""Delta Exchange public REST historical candles (recording only, no auth)."""

from __future__ import annotations

import json
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Protocol

from tradelab.data.schema import SOURCE_DELTA_REST, VENUE_DELTA_PUBLIC

DEFAULT_DELTA_REST_BASE = "https://api.delta.exchange"


class CandleHttpClient(Protocol):
    def get_json(self, url: str) -> object: ...


@dataclass(frozen=True, slots=True)
class OhlcvBar:
    symbol: str
    venue: str
    resolution: str
    open_time_ns: int
    open: float
    high: float
    low: float
    close: float
    volume: float
    source: str


def resolution_to_seconds(resolution: str) -> int:
    """Map Delta resolution strings to bar length in seconds."""
    known: dict[str, int] = {
        "1m": 60,
        "3m": 180,
        "5m": 300,
        "15m": 900,
        "30m": 1800,
        "1h": 3600,
        "2h": 7200,
        "4h": 14400,
        "6h": 21600,
        "1d": 86400,
        "7d": 604800,
    }
    if resolution not in known:
        msg = f"unsupported resolution: {resolution!r}"
        raise ValueError(msg)
    return known[resolution]


class UrllibJsonClient:
    """Default HTTP client using stdlib urllib (no extra dependencies)."""

    def __init__(self, timeout_s: float = 30.0) -> None:
        self._timeout_s = timeout_s

    def get_json(self, url: str) -> object:
        request = urllib.request.Request(url, method="GET", headers={"Accept": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=self._timeout_s) as response:
                payload = response.read()
        except urllib.error.HTTPError as exc:
            msg = f"Delta REST HTTP {exc.code} for {url}"
            raise RuntimeError(msg) from exc
        except urllib.error.URLError as exc:
            msg = f"Delta REST request failed for {url}: {exc.reason}"
            raise RuntimeError(msg) from exc
        return json.loads(payload)


def parse_candles_response(
    payload: object,
    *,
    symbol: str,
    resolution: str,
    venue: str = VENUE_DELTA_PUBLIC,
    source: str = SOURCE_DELTA_REST,
) -> list[OhlcvBar]:
    if not isinstance(payload, dict):
        msg = "expected JSON object response"
        raise ValueError(msg)
    if not payload.get("success"):
        msg = f"Delta API success=false: {payload!r}"
        raise ValueError(msg)
    raw_rows = payload.get("result")
    if not isinstance(raw_rows, list):
        msg = "expected result list"
        raise ValueError(msg)

    bars: list[OhlcvBar] = []
    for row in raw_rows:
        if not isinstance(row, dict):
            msg = f"unexpected candle row: {row!r}"
            raise ValueError(msg)
        time_s = int(row["time"])
        bars.append(
            OhlcvBar(
                symbol=symbol,
                venue=venue,
                resolution=resolution,
                open_time_ns=time_s * 1_000_000_000,
                open=float(row["open"]),
                high=float(row["high"]),
                low=float(row["low"]),
                close=float(row["close"]),
                volume=float(row["volume"]),
                source=source,
            )
        )
    bars.sort(key=lambda b: b.open_time_ns)
    return bars


def build_candles_url(
    *,
    base_url: str,
    symbol: str,
    resolution: str,
    start_s: int,
    end_s: int,
) -> str:
    query = urllib.parse.urlencode(
        {
            "symbol": symbol,
            "resolution": resolution,
            "start": str(start_s),
            "end": str(end_s),
        }
    )
    return f"{base_url.rstrip('/')}/v2/history/candles?{query}"


def fetch_candles_page(
    client: CandleHttpClient,
    *,
    base_url: str,
    symbol: str,
    resolution: str,
    start_s: int,
    end_s: int,
) -> list[OhlcvBar]:
    url = build_candles_url(
        base_url=base_url,
        symbol=symbol,
        resolution=resolution,
        start_s=start_s,
        end_s=end_s,
    )
    payload = client.get_json(url)
    return parse_candles_response(payload, symbol=symbol, resolution=resolution)


def fetch_candles_range(
    client: CandleHttpClient,
    *,
    base_url: str = DEFAULT_DELTA_REST_BASE,
    symbol: str,
    resolution: str,
    start_s: int,
    end_s: int,
    max_bars_per_request: int = 500,
) -> list[OhlcvBar]:
    """Walk forward in time, paging until ``end_s`` (inclusive window)."""
    if start_s >= end_s:
        return []
    interval = resolution_to_seconds(resolution)
    chunk_span_s = interval * max_bars_per_request
    cursor = start_s
    merged: dict[int, OhlcvBar] = {}

    while cursor < end_s:
        chunk_end = min(end_s, cursor + chunk_span_s)
        page = fetch_candles_page(
            client,
            base_url=base_url,
            symbol=symbol,
            resolution=resolution,
            start_s=cursor,
            end_s=chunk_end,
        )
        if not page:
            cursor = chunk_end + interval
            continue
        for bar in page:
            merged[bar.open_time_ns] = bar
        last_s = page[-1].open_time_ns // 1_000_000_000
        next_cursor = last_s + interval
        if next_cursor <= cursor:
            cursor = chunk_end + interval
        else:
            cursor = next_cursor

    return [merged[k] for k in sorted(merged)]
