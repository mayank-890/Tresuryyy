#!/usr/bin/env bash
# T-0006: migration, partition, idempotency, and privilege tests. Requires PostgreSQL via docker/compose.yml.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"
export PATH="/opt/homebrew/bin:/usr/local/bin:${PATH}"

# shellcheck source=db/partitions/lib.sh
source "${ROOT}/db/partitions/lib.sh"

if [[ -z "${DATABASE_URL:-}" ]]; then
  if [[ ! -f docker/.env ]]; then
    echo "docker/.env missing; copy docker/env.example to docker/.env and set TRADING_DEV_DB_PASSWORD" >&2
    exit 1
  fi
  load_database_url
fi

db_url_for_client() {
  printf '%s' "${1:-$DATABASE_URL}"
}

db_url_for_docker_client() {
  local url
  url="$(db_url_for_client "$1")"
  if [[ "$(uname -s)" == "Darwin" ]]; then
    url="${url/127.0.0.1/host.docker.internal}"
  fi
  printf '%s' "${url}"
}

psql_super() {
  if command -v psql >/dev/null 2>&1; then
    psql "$(db_url_for_client)" -v ON_ERROR_STOP=1 "$@"
  else
    docker run --rm postgres:16-bookworm psql "$(db_url_for_docker_client)" -v ON_ERROR_STOP=1 "$@"
  fi
}

psql_as() {
  local user="$1"
  local password="$2"
  shift 2
  local host port db
  host="$(db_url_for_client | sed -n 's#.*@\([^:/]*\).*#\1#p')"
  port="$(db_url_for_client | sed -n 's#.*:\([0-9]*\)/.*#\1#p')"
  db="$(db_url_for_client | sed -n 's#.*/\([^?]*\).*#\1#p')"
  port="${port:-5432}"
  db="${db:-trading_dev}"
  local url="postgres://${user}:${password}@${host}:${port}/${db}?sslmode=disable"
  if command -v psql >/dev/null 2>&1; then
    psql "${url}" -v ON_ERROR_STOP=1 "$@"
  else
    docker run --rm postgres:16-bookworm psql "${url}" -v ON_ERROR_STOP=1 "$@"
  fi
}

echo "== partition scripts =="
chmod +x db/partitions/*.sh
./db/partitions/create_daily_partitions.sh
./db/partitions/create_monthly_partitions.sh

today_suffix="$(date -u +%Y%m%d)"
daily_part="trading.market_ticks_${today_suffix}"
psql_super -c "SELECT to_regclass('${daily_part}') IS NOT NULL AS daily_partition_exists;" -tA | grep -qx t

month_suffix="$(date -u +%Y%m)"
monthly_part="trading.fills_${month_suffix}"
psql_super -c "SELECT to_regclass('${monthly_part}') IS NOT NULL AS monthly_partition_exists;" -tA | grep -qx t

echo "== ON CONFLICT DO NOTHING (market_ticks) =="
psql_super -c "DELETE FROM trading.market_ticks WHERE source_segment = 'test-seg';"
psql_super <<'SQL'
INSERT INTO trading.market_ticks (
    source_segment, source_sequence, event_time, instrument_id, last_ticks
) VALUES (
    'test-seg', 1, date_trunc('day', now() AT TIME ZONE 'UTC') AT TIME ZONE 'UTC' + interval '1 hour',
    42, 100
);
INSERT INTO trading.market_ticks (
    source_segment, source_sequence, event_time, instrument_id, last_ticks
) VALUES (
    'test-seg', 1, date_trunc('day', now() AT TIME ZONE 'UTC') AT TIME ZONE 'UTC' + interval '1 hour',
    42, 999
) ON CONFLICT (source_segment, source_sequence, event_time) DO NOTHING;
SELECT count(*) = 1 AS single_row
FROM trading.market_ticks
WHERE source_segment = 'test-seg' AND source_sequence = 1;
SQL
rows="$(psql_super -tA -c "SELECT count(*) FROM trading.market_ticks WHERE source_segment = 'test-seg' AND source_sequence = 1;")"
if [[ "${rows}" != "1" ]]; then
  echo "expected one market_ticks row after ON CONFLICT, got ${rows}" >&2
  exit 1
fi

echo "== role without privileges refused =="
psql_super <<'SQL'
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 't0006_no_priv') THEN
    DROP OWNED BY t0006_no_priv;
    DROP ROLE t0006_no_priv;
  END IF;
END $$;
CREATE ROLE t0006_no_priv LOGIN PASSWORD 'local-test-only';
SQL

set +e
psql_as t0006_no_priv local-test-only \
  -c "INSERT INTO trading.control_commands (source_segment, source_sequence, event_time, operator_id, command_type) VALUES ('x', 1, now(), 'op', 'HALT');" \
  2>/dev/null
denied=$?
set -e
if [[ "${denied}" -eq 0 ]]; then
  echo "expected INSERT as t0006_no_priv to fail" >&2
  exit 1
fi
echo "INSERT denied for t0006_no_priv (exit ${denied})"

psql_super -c "DROP ROLE IF EXISTS t0006_no_priv;"

echo "== drop partition without archive flag refused =="
set +e
./db/partitions/drop_partition.sh "${daily_part}" 2>/dev/null
drop_denied=$?
set -e
if [[ "${drop_denied}" -eq 0 ]]; then
  echo "expected drop without ARCHIVE_VERIFIED=1 to fail" >&2
  exit 1
fi
echo "drop refused without ARCHIVE_VERIFIED=1"

echo "All db migration tests passed."
