#!/usr/bin/env bash
# Create daily range partitions for high-volume tables, DAYS_AHEAD days beyond today (default 3).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=db/partitions/lib.sh
source "${ROOT}/db/partitions/lib.sh"
# shellcheck source=db/partitions/date_math.sh
source "${ROOT}/db/partitions/date_math.sh"

load_database_url

DAYS_AHEAD="${DAYS_AHEAD:-3}"
TABLES=(market_ticks candles_1m order_events)

for ((offset = 0; offset <= DAYS_AHEAD; offset++)); do
  start="$(partition_daily_range "${offset}")"
  end="$(partition_daily_range "$((offset + 1))")"
  suffix="${start//-/}"
  for table in "${TABLES[@]}"; do
    part="trading.${table}_${suffix}"
    psql_exec "CREATE TABLE IF NOT EXISTS ${part} PARTITION OF trading.${table}
      FOR VALUES FROM ('${start} 00:00:00+00') TO ('${end} 00:00:00+00');"
    echo "daily partition ready: ${part}"
  done
done
