"""Bearer token and CSRF checks for localhost control API."""

from __future__ import annotations

import secrets
from dataclasses import dataclass


@dataclass(frozen=True)
class AuthConfig:
    bearer_token: str
    csrf_token: str


def new_auth_config() -> AuthConfig:
    return AuthConfig(
        bearer_token=secrets.token_urlsafe(32),
        csrf_token=secrets.token_urlsafe(24),
    )


def authorize_bearer(header_value: str | None, expected: str) -> bool:
    if not header_value or not header_value.startswith("Bearer "):
        return False
    token = header_value.removeprefix("Bearer ").strip()
    return secrets.compare_digest(token, expected)


def authorize_csrf(header_value: str | None, expected: str) -> bool:
    if not header_value:
        return False
    return secrets.compare_digest(header_value.strip(), expected)
