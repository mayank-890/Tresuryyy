# Shared helpers for partition scripts (source from bash, do not execute directly).

normalize_host_url() {
  printf '%s' "${1/host.docker.internal/127.0.0.1}"
}

load_database_url() {
  if [[ -z "${DATABASE_URL:-}" ]]; then
    if [[ -f "${ROOT}/docker/.env" ]]; then
      set -a
      # shellcheck disable=SC1091
      source "${ROOT}/docker/.env"
      set +a
    elif [[ -f "${ROOT}/.env" ]]; then
      set -a
      # shellcheck disable=SC1091
      source "${ROOT}/.env"
      set +a
    fi
  fi
  if [[ -z "${DATABASE_URL:-}" ]]; then
    if [[ -z "${TRADING_DEV_DB_PASSWORD:-}" ]]; then
      echo "DATABASE_URL or TRADING_DEV_DB_PASSWORD must be set" >&2
      exit 1
    fi
    export DATABASE_URL="postgres://trading:${TRADING_DEV_DB_PASSWORD}@127.0.0.1:5432/trading_dev?sslmode=disable"
  fi
  export DATABASE_URL="$(normalize_host_url "${DATABASE_URL}")"
}

psql_exec() {
  export PATH="/opt/homebrew/bin:/usr/local/bin:${PATH}"
  if command -v psql >/dev/null 2>&1; then
    psql "${DATABASE_URL}" -v ON_ERROR_STOP=1 -c "$1"
  else
    local url="${DATABASE_URL}"
    if [[ "$(uname -s)" == "Darwin" ]]; then
      url="${url/127.0.0.1/host.docker.internal}"
    fi
    docker run --rm postgres:16-bookworm psql "${url}" -v ON_ERROR_STOP=1 -c "$1"
  fi
}
