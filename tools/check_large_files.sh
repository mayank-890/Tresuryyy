#!/usr/bin/env bash
# Fail if any tracked or staged file exceeds 1 MiB outside tests/fixtures/.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
max_bytes=$((1024 * 1024))
mode="${1:-all}"
fail=0

is_excluded() {
  local path="$1"
  case "$path" in
    tests/fixtures/*) return 0 ;;
    *) return 1 ;;
  esac
}

check_path() {
  local rel="$1"
  if is_excluded "$rel"; then
    return 0
  fi
  if [[ ! -f "$root/$rel" ]]; then
    return 0
  fi
  local size
  size="$(wc -c <"$root/$rel" | tr -d ' ')"
  if [[ "$size" -gt "$max_bytes" ]]; then
    printf 'LARGE FILE (%s bytes > %s): %s\n' "$size" "$max_bytes" "$rel"
    fail=1
  fi
}

if [[ "$mode" == "--staged" ]]; then
  if ! git -C "$root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    printf 'check_large_files: not a git repository\n' >&2
    exit 1
  fi
  found=0
  while IFS= read -r rel; do
    [[ -z "$rel" ]] && continue
    found=1
    check_path "$rel"
  done < <(git -C "$root" diff --cached --name-only --diff-filter=ACMR)
  if [[ "$found" -eq 0 ]]; then
    exit 0
  fi
else
  while IFS= read -r -d '' rel; do
    check_path "$rel"
  done < <(git -C "$root" ls-files -z)
fi

if [[ "$fail" -ne 0 ]]; then
  printf 'large-file check failed\n' >&2
  exit 1
fi

printf 'large-file check passed\n'
