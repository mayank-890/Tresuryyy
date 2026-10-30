"""Exponential backoff with jitter (TI §11)."""

from __future__ import annotations

import random


def reconnect_delay_s(attempt: int, *, base_s: float = 1.0, cap_s: float = 60.0) -> float:
    if attempt < 0:
        attempt = 0
    exp = min(cap_s, base_s * (2**attempt))
    jitter = exp * 0.2
    return max(0.0, exp + random.uniform(-jitter, jitter))
