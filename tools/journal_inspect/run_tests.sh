#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
python3 tools/journal_inspect/build_fixtures.py
python3 tools/journal_inspect/test_inspect.py
