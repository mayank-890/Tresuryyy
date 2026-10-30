"""Delta Exchange public WebSocket client (recording only)."""

from __future__ import annotations

import asyncio
import json
from collections.abc import AsyncIterator, Callable
from typing import Any

import websockets
from websockets.asyncio.client import ClientConnection

from tradelab_services.ingestion.backoff import reconnect_delay_s

DEFAULT_DELTA_PUBLIC_WS_URL = "wss://socket.delta.exchange"


def build_subscribe_payload(symbols: list[str]) -> dict[str, Any]:
    channels = [{"name": "v2_ticker", "symbols": symbols}]
    return {"type": "subscribe", "payload": {"channels": channels}}


async def iter_delta_public_messages(
    *,
    ws_url: str,
    symbols: list[str],
    should_run: Callable[[], bool],
    on_disconnect: Callable[[], None] | None = None,
) -> AsyncIterator[str]:
    """Yield raw WS text frames with automatic reconnect and backoff."""
    if not symbols:
        return
    attempt = 0
    while should_run():
        try:
            async with websockets.connect(ws_url, open_timeout=10) as ws:
                attempt = 0
                await ws.send(json.dumps(build_subscribe_payload(symbols)))
                async for message in _message_stream(ws):
                    if not should_run():
                        break
                    if isinstance(message, str):
                        yield message
                    elif isinstance(message, bytes):
                        yield message.decode("utf-8", errors="replace")
        except asyncio.CancelledError:
            raise
        except Exception:
            if on_disconnect is not None:
                on_disconnect()
            if not should_run():
                break
            delay = reconnect_delay_s(attempt)
            attempt += 1
            await asyncio.sleep(delay)


async def _message_stream(ws: ClientConnection) -> AsyncIterator[str | bytes]:
    async for message in ws:
        yield message
