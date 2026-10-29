# Task runner (TECHNICAL_IMPLEMENTATION.md §10.1). T-0003: build and test presets only.

set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

default_preset := "debug-asan"
vcpkg_root := env_var_or_default("VCPKG_ROOT", justfile_directory() / ".vcpkg")

export VCPKG_ROOT := vcpkg_root
export PATH := "/opt/homebrew/bin:" + env_var_or_default("PATH", "")

setup-vcpkg:
    if [[ ! -x "${VCPKG_ROOT}/vcpkg" ]]; then git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"; fi
    if [[ ! -x "${VCPKG_ROOT}/vcpkg" ]]; then "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics; fi

build preset=default_preset: setup-vcpkg
    cmake --preset "{{preset}}"
    cmake --build --preset "{{preset}}"

test preset=default_preset:
    just build "{{preset}}"
    ctest --preset "{{preset}}"

test-tsan:
    just build debug-tsan
    ctest --preset debug-tsan

check-watchdog preset=default_preset:
    tools/check_watchdog_links.sh "build/{{preset}}"

codegen:
    python3 schemas/codegen/generate.py --write

codegen-check:
    python3 schemas/codegen/generate.py --check

test-journal-schema:
    mkdir -p build
    g++ -std=c++20 -I engine/include -fsanitize=address,undefined schemas/tests/journal_round_trip_test.cpp -o build/journal_round_trip_test
    ./build/journal_round_trip_test
    python3 schemas/tests/test_journal_round_trip.py

research-dir := justfile_directory() / "research"

research-sync:
    cd "{{research-dir}}" && uv sync --dev --frozen

research-lint: research-sync
    cd "{{research-dir}}" && uv run ruff check src tests
    cd "{{research-dir}}" && uv run ruff format --check src tests
    cd "{{research-dir}}" && uv run mypy

research-test: research-sync
    cd "{{research-dir}}" && uv run pytest -q

services-test: research-sync
    cd "{{research-dir}}" && \
        PYTHONPATH="{{justfile_directory()}}/services/src:{{justfile_directory()}}/tools/journal_inspect" \
        uv run pytest -q "{{justfile_directory()}}/tests/unit/ingestion"

ui-dir := justfile_directory() / "ui"

ui-install:
    cd "{{ui-dir}}" && npm ci

ui-unit-test: ui-install
    cd "{{ui-dir}}" && npm run test:unit

ui-test: ui-install
    cd "{{ui-dir}}" && env -u FORCE_COLOR npm run test:e2e

ui-test-ci: ui-install
    cd "{{ui-dir}}" && npx playwright install chromium && npm run test:e2e

check preset=default_preset:
    just codegen-check
    just test-journal-schema
    just research-lint
    just research-test
    just services-test
    just ui-unit-test
    just test "{{preset}}"
    just check-watchdog "{{preset}}"

lint:
    just research-lint
    @echo "TODO(T-0002+): wire clang-format and clang-tidy"

check-all-presets:
    just check debug-asan
    just check debug-tsan
    just check debug-ubsan
    just check release
    just check bench
    just check coverage

db-env-file := justfile_directory() / "docker" / ".env"

dbmate-run command:
    #!/usr/bin/env bash
    set -euo pipefail
    eval "$(just load-db-env)"
    root="$(cd "$(dirname "{{justfile()}}")" && pwd)"
    dbmate_url="${DATABASE_URL}"
    if [[ "$(uname -s)" == "Darwin" ]]; then
      dbmate_url="${dbmate_url/127.0.0.1/host.docker.internal}"
    fi
    docker run --rm \
      -v "${root}/db/migrations:/db/migrations" \
      -e "DATABASE_URL=${dbmate_url}" \
      -e DBMATE_MIGRATIONS_TABLE=public.schema_migrations \
      -e DBMATE_NO_DUMP_SCHEMA=true \
      ghcr.io/amacneil/dbmate:2 \
      --migrations-dir /db/migrations \
      "{{command}}"

load-db-env:
    #!/usr/bin/env bash
    set -euo pipefail
    if [[ -f "{{db-env-file}}" ]]; then
      set -a
      source "{{db-env-file}}"
      set +a
    elif [[ -z "${DATABASE_URL:-}" ]]; then
      :
    fi
    if [[ -n "${DATABASE_URL:-}" ]]; then
      export DATABASE_URL="${DATABASE_URL/host.docker.internal/127.0.0.1}"
    fi
    if [[ -z "${DATABASE_URL:-}" ]]; then
      if [[ -z "${TRADING_DEV_DB_PASSWORD:-}" ]]; then
        echo "Set TRADING_DEV_DB_PASSWORD in docker/.env or export DATABASE_URL" >&2
        exit 1
      fi
      export DATABASE_URL="postgres://trading:${TRADING_DEV_DB_PASSWORD}@127.0.0.1:5432/trading_dev?sslmode=disable"
    fi
    printf 'export DATABASE_URL=%q\n' "${DATABASE_URL}"

db-up:
    #!/usr/bin/env bash
    set -euo pipefail
    args=(-f docker/compose.yml)
    if [[ -f docker/.env ]]; then
      args+=(--env-file docker/.env)
    fi
    docker compose "${args[@]}" up -d --wait

db-down:
    #!/usr/bin/env bash
    set -euo pipefail
    args=(-f docker/compose.yml)
    if [[ -f docker/.env ]]; then
      args+=(--env-file docker/.env)
    fi
    docker compose "${args[@]}" down -v

migrate:
    just dbmate-run up

migrate-down:
    just dbmate-run down

db-migrate-cycle: migrate migrate-down migrate

test-db: migrate
    #!/usr/bin/env bash
    set -euo pipefail
    chmod +x db/partitions/*.sh db/tests/run_migration_tests.sh
    eval "$(just load-db-env)"
    ./db/tests/run_migration_tests.sh
