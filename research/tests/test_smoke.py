"""Research package smoke tests (T-0009)."""

from __future__ import annotations

import pytest


def test_engine_stub_references_adr_0005() -> None:
    from tradelab.engine import load_native

    with pytest.raises(NotImplementedError, match="ADR-0005"):
        load_native()
