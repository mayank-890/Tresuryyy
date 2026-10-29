#!/usr/bin/env bash
# Drop a partition only when archive verification is recorded (ARCHIVE_VERIFIED=1).
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 trading.table_YYYYMMDD" >&2
  exit 1
fi

PART="$1"

if [[ "${ARCHIVE_VERIFIED:-0}" != "1" ]]; then
  echo "refused: set ARCHIVE_VERIFIED=1 after archive verification to drop ${PART}" >&2
  exit 1
fi

if [[ ! "${PART}" =~ ^trading\.[a-z_0-9]+$ ]]; then
  echo "refused: invalid partition name ${PART}" >&2
  exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=db/partitions/lib.sh
source "${ROOT}/db/partitions/lib.sh"

load_database_url

psql_exec "DROP TABLE IF EXISTS ${PART};"
echo "dropped partition ${PART} (archive verified)"
