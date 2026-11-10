"""Integration: HTTP control API journals and forwards over UDS."""

from __future__ import annotations

import json
import os
import socket
import threading
from pathlib import Path

from tradelab_services.control_api.app import ControlApiApp, ControlApiConfig
from tradelab_services.control_api.auth import AuthConfig
from tradelab_services.journal import generated as g
from test_control_api import _request


def _uds_stub(
    socket_path: Path,
    seen: list[dict[str, object]],
    stop: threading.Event,
    ready: threading.Event,
) -> None:
    if socket_path.exists():
        socket_path.unlink()
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(str(socket_path))
    server.listen(1)
    server.settimeout(0.5)
    ready.set()
    try:
        while not stop.is_set():
            try:
                conn, _ = server.accept()
            except OSError:
                continue
            with conn:
                data = b""
                while True:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    data += chunk
                line = data.decode("utf-8").strip()
                body = json.loads(line)
                seen.append(body)
                conn.sendall(b'{"accepted":true,"detail":"stub"}\n')
    finally:
        server.close()
        if socket_path.exists():
            socket_path.unlink()


def test_halt_journaled_and_forwarded(tmp_path: Path) -> None:
    journal = tmp_path / "control.journal"
    # macOS AF_UNIX path length limit; keep the socket under /tmp.
    repo_root = Path(__file__).resolve().parents[2]
    sock_dir = repo_root / "build"
    sock_dir.mkdir(parents=True, exist_ok=True)
    uds_path = sock_dir / f"ctl-{os.getpid()}.sock"
    seen: list[dict[str, object]] = []
    stop = threading.Event()
    ready = threading.Event()
    stub = threading.Thread(target=_uds_stub, args=(uds_path, seen, stop, ready), daemon=True)
    stub.start()
    assert ready.wait(timeout=2.0)
    auth = AuthConfig(bearer_token="bearer-1", csrf_token="csrf-1")
    app = ControlApiApp(
        ControlApiConfig(
            journal_path=journal,
            control_socket=str(uds_path),
            auth=auth,
            port=0,
            operator_id="ops",
        )
    )
    app.start()
    base = f"http://127.0.0.1:{app.port}"
    try:
        code, payload = _request(
            f"{base}/api/v1/halt",
            method="POST",
            token="bearer-1",
            csrf="csrf-1",
            body={
                "operator_id": "ops",
                "confirmed": True,
                "confirmation_phrase": "HALT",
            },
        )
        assert code == 200
        assert payload["journal_sequence"] == 1
        assert payload["uds_detail"] == "stub"
    finally:
        app.shutdown()
        stop.set()
        stub.join(timeout=2.0)
    assert len(seen) == 1
    assert seen[0]["command"] == "HALT"
    assert seen[0]["operator_ref"] == "ops"
    raw = journal.read_bytes()
    assert g.JOURNAL_MAGIC.encode("ascii") in raw
    offset = raw.find(g.JOURNAL_MAGIC.encode("ascii"))
    header = g.decode_header(raw[offset : offset + 40])
    assert header.record_type == int(g.RecordType.ControlCommand)
    command = g.decode_control_command(raw[offset + 40 : offset + 40 + header.payload_length])
    assert command.operator_ref == "ops"
    assert command.command == int(g.ControlCommandKind.Halt)
    assert command.outcome == int(g.CommandOutcome.Accepted)
