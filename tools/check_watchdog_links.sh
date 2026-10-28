#!/usr/bin/env bash
# Fail if the watchdog executable links trading_strategy or trading_risk (TI §3.3, ADR-0010).
set -euo pipefail

if [[ $# -lt 1 ]]; then
  printf 'usage: %s <cmake-build-dir>\n' "$0" >&2
  exit 2
fi

build_dir="$1"
build_dir="$(cd "$build_dir" && pwd)"

link_line=""
link_file=""
while IFS= read -r candidate; do
  link_file="$candidate"
  break
done < <(find "$build_dir" -path '*/watchdog.dir/link.txt' 2>/dev/null | head -n 1)

if [[ -n "$link_file" && -f "$link_file" ]]; then
  link_line="$(cat "$link_file")"
elif [[ -f "$build_dir/build.ninja" ]]; then
  link_line="$(grep -E '^build engine/watchdog: CXX_EXECUTABLE_LINKER' "$build_dir/build.ninja" || true)"
fi

if [[ -z "$link_line" ]]; then
  printf 'watchdog link check: could not find watchdog link line under %s\n' "$build_dir" >&2
  exit 1
fi

if grep -E 'trading_strategy|trading_risk' <<<"$link_line"; then
  printf 'watchdog link check failed: forbidden library in link line\n' >&2
  exit 1
fi

printf 'watchdog link check passed\n'
