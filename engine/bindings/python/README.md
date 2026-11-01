pybind11 module for the strategy library.

This stub exposes `FlatEcho` only. It is built when `TRADING_BUILD_BINDINGS=ON`.
Default presets leave that option OFF.

pybind11 3.1.0 is fetched at configure time (same tag and SHA512 as the vcpkg port).
It is not a `vcpkg.json` dependency: that port pulls vcpkg's own Python, which is
recorded as Q-24. ADR-0005 is the Accepted binding decision and is still a stub.

Build and import, from the repository root, with the research interpreter:

```bash
export VCPKG_ROOT="${VCPKG_ROOT:-$PWD/.vcpkg}"
PYTHON="$(cd research && uv run python -c 'import sys; print(sys.executable)')"
cmake -S . -B build/bindings -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DTRADING_BUILD_BINDINGS=ON \
  -DTRADING_ENABLE_GROWW=OFF \
  -DTRADING_ENABLE_LIVE_PROFILE=OFF \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPython_EXECUTABLE="$PYTHON"
cmake --build build/bindings --target trading_bindings
cd research && uv run python -c 'from tradelab.engine import load_native; m = load_native(); print(m.FlatEcho(1).name())'
```

`load_native()` imports `build/bindings/**/trading_bindings*.so`. The module does not
place orders. `FlatEcho` is a sample strategy and is not for trading.
