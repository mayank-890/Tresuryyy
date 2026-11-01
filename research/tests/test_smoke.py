"""Research package smoke tests (T-0009)."""

from __future__ import annotations

import inspect

import pytest

from tradelab.engine import find_bindings_module, load_native


def test_engine_stub_references_adr_0005() -> None:
    """A missing module still cites ADR-0005. A built module exposes FlatEcho."""

    assert "ADR-0005" in inspect.getsource(load_native)
    if find_bindings_module() is None:
        with pytest.raises(NotImplementedError, match="ADR-0005"):
            load_native()
        return
    assert hasattr(load_native(), "FlatEcho")
