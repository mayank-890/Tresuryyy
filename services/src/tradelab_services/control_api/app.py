"""HTTP control API (127.0.0.1 only). No order submission routes."""

from __future__ import annotations

import json
import threading
import time
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

from tradelab_services.control_api.audit import ControlAuditLog
from tradelab_services.control_api.auth import (
    AuthConfig,
    authorize_bearer,
    authorize_csrf,
    new_auth_config,
)
from tradelab_services.control_api.commands import (
    CONFIRMATION_PHRASES,
    REQUIRES_CONFIRMATION,
    CommandRequest,
    ControlScopeName,
    HttpCommand,
)
from tradelab_services.control_api.state import ControlState
from tradelab_services.control_api.uds_client import forward_command


@dataclass(frozen=True)
class ControlApiConfig:
    host: str = "127.0.0.1"
    port: int = 0
    journal_path: Path | None = None
    control_socket: str = ""
    operator_id: str = "operator"
    auth: AuthConfig | None = None


ALLOWED_POST_PATHS = frozenset(
    {
        "/api/v1/halt",
        "/api/v1/resume",
        "/api/v1/cancel-all",
        "/api/v1/kill",
    }
)


class ControlApiApp:
    def __init__(self, config: ControlApiConfig) -> None:
        self._config = config
        self._auth = config.auth or new_auth_config()
        self._state = ControlState()
        self._audit: ControlAuditLog | None = None
        if config.journal_path is not None:
            self._audit = ControlAuditLog(config.journal_path)
        self._server: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None

    @property
    def auth(self) -> AuthConfig:
        return self._auth

    @property
    def port(self) -> int:
        if self._server is None:
            return self._config.port
        return int(self._server.server_address[1])

    def start(self, *, daemon: bool = True) -> None:
        handler = _make_handler(self)
        self._server = ThreadingHTTPServer((self._config.host, self._config.port), handler)
        self._thread = threading.Thread(target=self._server.serve_forever, name="control-api", daemon=daemon)
        self._thread.start()

    def shutdown(self) -> None:
        if self._server is not None:
            self._server.shutdown()
            self._server.server_close()
            self._server = None
        if self._thread is not None:
            self._thread.join(timeout=5.0)
            self._thread = None
        if self._audit is not None:
            self._audit.close()

    def handle_command(self, command: HttpCommand, body: dict[str, Any]) -> tuple[int, dict[str, Any]]:
        scope_name = str(body.get("scope", "global"))
        try:
            scope = ControlScopeName(scope_name)
        except ValueError:
            return HTTPStatus.BAD_REQUEST, {"error": "invalid_scope"}
        operator_ref = str(body.get("operator_id", self._config.operator_id))[:32]
        confirmed = bool(body.get("confirmed", False))
        phrase = str(body.get("confirmation_phrase", ""))
        if command in REQUIRES_CONFIRMATION:
            expected = CONFIRMATION_PHRASES[command]
            if not confirmed or phrase != expected:
                return HTTPStatus.PRECONDITION_REQUIRED, {
                    "error": "confirmation_required",
                    "confirmation_phrase": expected,
                }
        req = CommandRequest(
            command=command,
            scope=scope,
            operator_ref=operator_ref,
            scope_ref=str(body.get("scope_ref", "")),
            confirmed=confirmed,
        )
        journal_sequence = 0
        if self._audit is not None:
            result = self._audit.append_accepted(req)
            journal_sequence = result.sequence
        uds_detail = "skipped"
        if self._config.control_socket:
            forward = forward_command(
                self._config.control_socket,
                req,
                session_token=self._auth.bearer_token,
            )
            uds_detail = forward.detail
            if not forward.accepted:
                return HTTPStatus.BAD_GATEWAY, {
                    "error": "engine_rejected",
                    "detail": uds_detail,
                    "journal_sequence": journal_sequence,
                }
        self._state.apply_command(
            command.value,
            operator_ref=operator_ref,
            journal_sequence=journal_sequence,
        )
        return HTTPStatus.OK, {
            "accepted": True,
            "command": command.value,
            "journal_sequence": journal_sequence,
            "uds_detail": uds_detail,
        }

    def status_payload(self) -> dict[str, Any]:
        return {
            "run_state": "halted" if self._state.halted else "running",
            "halted": self._state.halted,
            "last_command": self._state.last_command,
            "last_operator": self._state.last_operator,
            "last_journal_sequence": self._state.last_journal_sequence,
            "status_wall_ns": self._state.updated_wall_ns,
            "csrf_token": self._auth.csrf_token,
        }


def _make_handler(app: ControlApiApp) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args: Any) -> None:  # noqa: A003
            return

        def _unauthorized(self) -> None:
            self.send_response(HTTPStatus.UNAUTHORIZED)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"error":"unauthorized"}')

        def _forbidden_csrf(self) -> None:
            self.send_response(HTTPStatus.FORBIDDEN)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"error":"csrf_required"}')

        def _method_not_allowed(self) -> None:
            self.send_response(HTTPStatus.METHOD_NOT_ALLOWED)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"error":"method_not_allowed"}')

        def _not_found(self) -> None:
            self.send_response(HTTPStatus.NOT_FOUND)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"error":"not_found"}')

        def _json_response(self, code: int, payload: dict[str, Any]) -> None:
            body = json.dumps(payload).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _check_auth(self) -> bool:
            if not authorize_bearer(self.headers.get("Authorization"), app._auth.bearer_token):
                self._unauthorized()
                return False
            if self.command in {"POST", "PUT", "PATCH", "DELETE"}:
                if not authorize_csrf(self.headers.get("X-CSRF-Token"), app._auth.csrf_token):
                    self._forbidden_csrf()
                    return False
            return True

        def do_GET(self) -> None:  # noqa: N802
            path = urlparse(self.path).path
            if path == "/api/v1/status":
                if not self._check_auth():
                    return
                self._json_response(HTTPStatus.OK, app.status_payload())
                return
            if path.startswith("/api/v1/order") or "submit" in path:
                self._not_found()
                return
            self._not_found()

        def do_POST(self) -> None:  # noqa: N802
            path = urlparse(self.path).path
            if path not in ALLOWED_POST_PATHS:
                if path.startswith("/api/v1/order"):
                    self._not_found()
                    return
                self._not_found()
                return
            if not self._check_auth():
                return
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length) if length else b"{}"
            try:
                body = json.loads(raw.decode("utf-8"))
            except json.JSONDecodeError:
                self._json_response(HTTPStatus.BAD_REQUEST, {"error": "invalid_json"})
                return
            if not isinstance(body, dict):
                self._json_response(HTTPStatus.BAD_REQUEST, {"error": "invalid_body"})
                return
            command_map = {
                "/api/v1/halt": HttpCommand.HALT,
                "/api/v1/resume": HttpCommand.RESUME,
                "/api/v1/cancel-all": HttpCommand.CANCEL_ALL,
                "/api/v1/kill": HttpCommand.KILL,
            }
            code, payload = app.handle_command(command_map[path], body)
            self._json_response(code, payload)

    return Handler


def serve_forever(config: ControlApiConfig) -> None:
    app = ControlApiApp(config)
    app.start(daemon=False)
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        pass
    finally:
        app.shutdown()
