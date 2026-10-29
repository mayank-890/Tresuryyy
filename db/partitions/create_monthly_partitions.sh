#!/usr/bin/env bash
# Create monthly range partitions for lower-volume tables, MONTHS_AHEAD months beyond this month (default 2).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=db/partitions/lib.sh
source "${ROOT}/db/partitions/lib.sh"
# shellcheck source=db/partitions/date_math.sh
source "${ROOT}/db/partitions/date_math.sh"

load_database_url

MONTHS_AHEAD="${MONTHS_AHEAD:-2}"
TABLES=(fills position_snapshots risk_events audit_log)

for ((offset = 0; offset <= MONTHS_AHEAD; offset++)); do
  start="$(partition_month_start "${offset}")"
  end="$(partition_month_start "$((offset + 1))")"
  suffix="$(partition_month_suffix "${offset}")"
  for table in "${TABLES[@]}"; do
    part="trading.${table}_${suffix}"
    psql_exec "CREATE TABLE IF NOT EXISTS ${part} PARTITION OF trading.${table}
      FOR VALUES FROM ('${start} 00:00:00+00') TO ('${end} 00:00:00+00');"
    echo "monthly partition ready: ${part}"
  done
done
