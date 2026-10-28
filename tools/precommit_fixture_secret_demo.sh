#!/usr/bin/env bash
# Dry-run pre-commit against a synthetic secret pattern (not committed).
# Used for T-0002 evidence. Does not modify the repository.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fixture="$root/tools/_precommit_demo_ephemeral.txt"

if ! command -v pre-commit >/dev/null 2>&1; then
  printf 'pre-commit is not installed. Run: python3 -m pip install pre-commit\n' >&2
  exit 127
fi

# Build a synthetic high-entropy api_key at runtime (never stored in this repository).
hex_body="$(hexdump -vn 32 -e '/1 "%02x"' /dev/urandom)"
printf '%s\n' "api_key=${hex_body}" >"$fixture"

cleanup() {
  rm -f "$fixture"
}
trap cleanup EXIT

cd "$root"
pre-commit install >/dev/null 2>&1 || true
git add "$fixture"
set +e
pre-commit run gitleaks
exit_code=$?
set -e
git reset HEAD -- "$fixture" >/dev/null 2>&1 || true

if [[ "$exit_code" -ne 0 ]]; then
  printf 'fixture secret demo: pre-commit blocked the pattern (exit %s)\n' "$exit_code"
  exit 0
fi

printf 'fixture secret demo: expected pre-commit to fail but exit was %s\n' "$exit_code" >&2
exit 1
