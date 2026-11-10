"""Forward control commands to the engine UDS (line protocol v1)."""

from __future__ import annotations

import json
import socket
from dataclasses import dataclass

from tradelab_services.control_api.commands import CommandRequest, uds_line_payload


@dataclass(frozen=True)
class UdsForwardResult:
    accepted: bool
    detail: str


def forward_command(
    socket_path: str,
    req: CommandRequest,
    *,
    session_token: str,
    timeout_s: float = 2.0,
) -> UdsForwardResult:
    line = json.dumps(uds_line_payload(req, session_token=session_token), separators=(",", ":"))
    line += "\n"
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(timeout_s)
            sock.connect(socket_path)
            sock.sendall(line.encode("utf-8"))
            sock.shutdown(socket.SHUT_WR)
            response = sock.recv(4096)
    except OSError as exc:
        return UdsForwardResult(accepted=False, detail=f"uds_error:{exc}")
    if not response:
        return UdsForwardResult(accepted=True, detail="no_response")
    text = response.decode("utf-8", errors="replace").strip()
    try:
        body = json.loads(text)
    except json.JSONDecodeError:
        return UdsForwardResult(accepted=False, detail=f"bad_response:{text[:120]}")
    accepted = bool(body.get("accepted", False))
    detail = str(body.get("detail", ""))
    return UdsForwardResult(accepted=accepted, detail=detail)
