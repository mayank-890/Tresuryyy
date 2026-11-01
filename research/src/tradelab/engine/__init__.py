"""Load the pybind11 strategy module when it has been built."""

from __future__ import annotations

import importlib.util
import sys
from importlib.machinery import ModuleSpec
from pathlib import Path
from types import ModuleType

_REPO_ROOT = Path(__file__).resolve().parents[4]


def bindings_search_root() -> Path:
    """Directory that receives `-DTRADING_BUILD_BINDINGS=ON` builds."""

    return _REPO_ROOT / "build" / "bindings"


def find_bindings_module() -> Path | None:
    """Return the newest `trading_bindings` extension, if one has been built."""

    root = bindings_search_root()
    if not root.is_dir():
        return None
    matches = sorted(root.rglob("trading_bindings*.so"), key=lambda path: path.stat().st_mtime)
    if not matches:
        return None
    return matches[-1]


def load_native() -> ModuleType:
    """Import `trading_bindings`.

    Build it first. See `engine/bindings/python/README.md`.
    pybind11 is the ADR-0005 library. Q-24 records why it is fetched at configure
    time instead of installed from the vcpkg manifest.
    """

    path = find_bindings_module()
    if path is None:
        raise NotImplementedError(
            "trading_bindings is not built. Configure with -DTRADING_BUILD_BINDINGS=ON "
            "(ADR-0005, Q-24). See engine/bindings/python/README.md."
        )
    spec: ModuleSpec | None = importlib.util.spec_from_file_location("trading_bindings", path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load trading_bindings from {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules["trading_bindings"] = module
    spec.loader.exec_module(module)
    return module
