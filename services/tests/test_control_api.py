"""Unit tests for control API (auth, confirmation, journaling, no order routes)."""

from __future__ import annotations

import json
import urllib.error
import urllib.request
from pathlib import Path

import pytest

from tradelab_services.control_api.app import ControlApiApp, ControlApiConfig
from tradelab_services.control_api.auth import AuthConfig, authorize_bearer, authorize_csrf
from tradelab_services.journal import generated as g


def _request(
    url: str,
    *,
    method: str,
    token: str,
    csrf: str,
    body: dict | None = None,
) -> tuple[int, dict]:
    data = None
    headers = {
        "Authorization": f"Bearer {token}",
        "X-CSRF-Token": csrf,
    }
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        payload = json.loads(exc.read().decode("utf-8"))
        return exc.code, payload


def test_bearer_and_csrf_helpers() -> None:
    auth = AuthConfig(bearer_token="secret-token", csrf_token="csrf-1")
    assert authorize_bearer("Bearer secret-token", auth.bearer_token)
    assert not authorize_bearer("Bearer wrong", auth.bearer_token)
    assert authorize_csrf("csrf-1", auth.csrf_token)
    assert not authorize_csrf("nope", auth.csrf_token)


def test_kill_requires_confirmation(tmp_path: Path) -> None:
    journal = tmp_path / "ctl.journal"
    auth = AuthConfig(bearer_token="tok", csrf_token="csrf")
    app = ControlApiApp(
        ControlApiConfig(journal_path=journal, auth=auth, port=0, control_socket=""),
    )
    app.start()
    base = f"http://127.0.0.1:{app.port}"
    try:
        code, payload = _request(
            f"{base}/api/v1/kill",
            method="POST",
            token="tok",
            csrf="csrf",
            body={"operator_id": "op-a"},
        )
        assert code == 428
        assert payload["confirmation_phrase"] == "KILL"
        code, payload = _request(
            f"{base}/api/v1/kill",
            method="POST",
            token="tok",
            csrf="csrf",
            body={"operator_id": "op-a", "confirmed": True, "confirmation_phrase": "KILL"},
        )
        assert code == 200
        assert payload["accepted"] is True
        assert payload["journal_sequence"] == 1
    finally:
        app.shutdown()
    raw = journal.read_bytes()
    assert g.JOURNAL_MAGIC.encode("ascii") in raw


def test_status_and_no_order_route(tmp_path: Path) -> None:
    auth = AuthConfig(bearer_token="tok", csrf_token="csrf")
    app = ControlApiApp(ControlApiConfig(journal_path=tmp_path / "x.journal", auth=auth, port=0))
    app.start()
    base = f"http://127.0.0.1:{app.port}"
    try:
        code, payload = _request(f"{base}/api/v1/status", method="GET", token="tok", csrf="csrf")
        assert code == 200
        assert "csrf_token" in payload
        req = urllib.request.Request(
            f"{base}/api/v1/orders/submit",
            method="POST",
            headers={"Authorization": "Bearer tok", "X-CSRF-Token": "csrf"},
            data=b"{}",
        )
        with pytest.raises(urllib.error.HTTPError) as exc:
            urllib.request.urlopen(req, timeout=5)
        assert exc.value.code == 404
    finally:
        app.shutdown()


def test_unauthorized_without_bearer(tmp_path: Path) -> None:
    app = ControlApiApp(
        ControlApiConfig(
            journal_path=tmp_path / "y.journal",
            auth=AuthConfig(bearer_token="tok", csrf_token="csrf"),
            port=0,
        )
    )
    app.start()
    base = f"http://127.0.0.1:{app.port}"
    try:
        req = urllib.request.Request(f"{base}/api/v1/status", method="GET")
        with pytest.raises(urllib.error.HTTPError) as exc:
            urllib.request.urlopen(req, timeout=5)
        assert exc.value.code == 401
    finally:
        app.shutdown()
