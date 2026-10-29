"""Load journal v1 generated bindings from the committed services module."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]
_GENERATED = _REPO_ROOT / "services/src/tradelab_services/journal/generated.py"


def load_generated():
    spec = importlib.util.spec_from_file_location("tradelab_journal_generated", _GENERATED)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load journal bindings from {_GENERATED}")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod
