# PROGRESS.md

Single source of truth for the current state of work. Read this first in every session. Update it at every checkpoint.

## Handoff
- Updated: 2026-10-31T19:20:00+0530 by cursor
- Active ticket: **none**
- Branch: `main`
- Last green commit: this merge `Merge T-0108: projector stub` (feature `3b5fb1f`). T-0107 is already on `main` at `d773db8` (fast-forward; feature `70378ba`).
- Working tree: clean after the merge commit
- Tests passing at last commit: `just check` after this merge includes journal-recovery **9/9** and projector unit **9/9**. `just projector-integration` **1/1** was green on `3b5fb1f` (not part of `just check`).
- Next step (exact): No ticket is Ready. Operator writes the Phase 1 exit note. Then draft the Phase 2 strategy interface (PROCESS §6.1). Remaining Phase 1 exit work is operational: ≥2 weeks of capture (Q-16, Q-18) and DQ/retention (Q-19, Q-20).
- Files touched this merge: `PROGRESS.md`, `justfile`, and the T-0108 projector files from `feat/T-0108-projector-stub`
- Decisions made this session (not yet in ADR): none. Recovery stays copy-safe truncate. Projector still uses `psql` (Q-23). `event_time` is integer `wall_ns` → UTC (`1705326896789000000` is `2024-01-15 13:54:56.789+00`).
- Open questions: Q-22, Q-23; Q-16, Q-18, Q-21; Q-17, Q-19, Q-20; Q-1–Q-7, Q-11, Q-13, Q-14; B-0101-1
- Notes for the next tool: `just check` runs `journal-recovery-test` and `projector-test`. `just projector-integration` needs Postgres. Dry-run projector: `PYTHONPATH=services/src:tools:tools/journal_inspect python3 -m tradelab_services.projector --segment <file> --dry-run`. Recovery dry-run: `PYTHONPATH=tools:tools/journal_inspect python3 -m journal_recover <segment>`.

## Current Phase

**Phase 1: Data infrastructure — in progress.** Phase 0 exit recorded 2026-10-29 (`docs/reviews/phase-0.md`; operator sign-off pending). **T-0101** Done @ `de58b1c`. **T-0102** Done @ `72e7380`. **T-0103** Done @ `049ff86`. **T-0104** Done @ `dbeacdb`. **T-0105** Done @ `cddb2e9`. **T-0106** Done @ `67ad621`. **T-0107** Done @ `d773db8`. **T-0108** Done in this merge (feature `3b5fb1f`).

## Phase 0 Backlog

Status values: `Backlog` · `Ready` · `In progress` · `In review` · `Done` · `Blocked`

### T-0001: Repository skeleton
- Status: Done
- Phase: 0 · Risk class: C · Depends on: none
- Requirements: PRD §5 (roles), TI §2 (layout)
- Goal: Repository initialized with the full document set and the directory skeleton from the repository structure. No code.
- Acceptance criteria:
  - [x] Git repository initialized; `main` branch exists; first commit contains only the documents and `.gitignore`
  - [x] `.gitignore` excludes: build outputs, `.handoff/`, `.venv/`, `*.env`, `secrets/`, `data/`, `*.journal`, IDE and tool-local settings
  - [x] `.editorconfig` present (UTF-8, LF, final newline, 4-space C++, 4-space Python, 2-space YAML/TOML/JSON)
  - [x] Directory skeleton exists with `README.md` placeholders in each top-level folder: `engine/`, `research/`, `services/`, `ui/`, `schemas/`, `proto/`, `db/`, `config/`, `deploy/`, `tests/`, `tools/`, `docs/`, `docker/`, `.github/workflows/`
  - [x] `docs/adr/` contains ADR-0001 to ADR-0012 as stubs with status and title from DESIGN_ARCHITECTURE.md §18
  - [x] `docs/tickets/` and `docs/reviews/` exist with `.gitkeep`
  - [x] `README.md` at root: one-paragraph description, pointer to `AGENTS.md`, pointer to the docs set, status "Phase 0"
  - [x] `CHANGELOG.md` with an `Unreleased` section
- Evidence:
  - `main` is `8086b9fdb8a63bcef4461950ccf1b759449df7b2`, message `T-0001: add document set and gitignore`. `git show --name-only` lists only `.gitignore`, `AGENTS.md`, `AGENT_PROMPTS.md`, `DESIGN_ARCHITECTURE.md`, `PRD.md`, `PROCESS_PROJECT_MANAGEMENT.md`, `PROGRESS.md`, `REPO_STRUCTURE.md`, `TECHNICAL_IMPLEMENTATION.md`, `TESTING_QA.md`, and `USER_OPERATIONS.md`.
  - `.gitignore` excludes `build/`, `out/`, `cmake-build-*/`, `.handoff/`, `.venv/`, `__pycache__/`, `node_modules/`, `*.env`, `.env`, `/secrets/`, `/data/`, `*.journal`, `*.parquet` except `tests/fixtures/**/*.parquet`, and `.vscode/`, `.idea/`, `.cursor/`, `.codex/`. `/data/` and `/secrets/` are anchored so required skeleton paths stay tracked. See Q-7.
  - `.editorconfig` sets UTF-8, LF, a final newline, trimmed trailing whitespace, 4-space C++ and Python, and 2-space YAML, TOML, and JSON.
  - Nested directories from REPO_STRUCTURE.md §2–§6 exist. A directory with no other file has a one- or two-line `README.md`. `tools/README.md` states the directory purpose, which the acceptance criterion requires for that top-level folder. `engine/src/risk/` has `limits.hpp` and no README.
  - ADR stubs ADR-0001 through ADR-0012 use the template in PROCESS_PROJECT_MANAGEMENT.md §5.6. Titles and statuses match DESIGN_ARCHITECTURE.md §18, including Proposed for ADR-0011 and Pending for ADR-0012. ADR-0012 contains no numeric limits.
  - `docs/tickets/.gitkeep` and `docs/reviews/.gitkeep` exist.
  - Root `README.md` has one description paragraph, a pointer to `AGENTS.md`, the document list, and `Status: Phase 0`.
  - `CHANGELOG.md` contains `## Unreleased`.
  - `tools/check_repo_skeleton.sh` is executable and printed `skeleton check passed (287 paths)`.
  - Merged to `main`: `09cfc893` (`T-0001: merge repository skeleton into main`, 2026-10-29).
- Files allowed: the files in the acceptance criteria, plus the nested directories required by the operator instructions and REPO_STRUCTURE.md §2–§6, ADR stubs, comment-only `engine/src/risk/limits.hpp`, and `tools/check_repo_skeleton.sh`. The ticket text did not list every nested path; those paths are required by REPO_STRUCTURE.md and the operator session instructions.
- Out of scope: any source code, build configuration, CI, database, config profiles
- Tests required: none beyond a script check that every required file exists (`tools/check_repo_skeleton.sh`, added in this ticket)

### T-0002: Hooks, secret scanning, CI skeleton
- Status: Done
- Phase: 0 · Risk class: C (CI safety gates: A) · Depends on: T-0001
- Requirements: NFR-SEC-01, TI §10.2, TI §10.3, TESTING_QA §10
- Goal: Pre-commit hooks and a CI workflow that run on every PR and currently build nothing.
- Acceptance criteria:
  - [x] `.pre-commit-config.yaml` with: gitleaks, large-file check (>1 MB outside `tests/fixtures/`), trailing whitespace, end-of-file fixer
  - [x] `.gitleaks.toml` with repository allowlist empty
  - [x] `.github/workflows/ci.yml` runs: secret scan, large-file check, skeleton check (T-0001 script); all required-check names reserved (job names may be placeholders that pass with a `TODO(T-0003)` note)
  - [x] Demonstrated: a test commit containing a fake secret pattern is blocked locally by pre-commit (record the command and output in the PR)
- Evidence:
  - `.pre-commit-config.yaml`: gitleaks v8.21.2 with `.gitleaks.toml`; `trailing-whitespace`, `end-of-file-fixer`, `check-added-large-files` (excludes `tests/fixtures/`), local `tools/check_large_files.sh --staged`.
  - `.gitleaks.toml`: `[allowlist]` `paths`, `regexes`, and `stopwords` are empty arrays; `[extend] useDefault = true`.
  - `.github/workflows/ci.yml`: jobs `secret-scan`, `large-file-check`, `skeleton-check` run real checks; TESTING_QA.md §10 job IDs are present as placeholders (`build-debug-asan` through `coverage-thresholds`).
  - Secret demo command: `PATH=.venv/bin:$PATH ./tools/precommit_fixture_secret_demo.sh`. Output included `Detect hardcoded secrets... Failed`, gitleaks exit 1, `RuleID: generic-api-key`, file `tools/_precommit_demo_ephemeral.txt` (removed after run; not committed).
  - `actionlint`: not available on PATH (recorded; not blocking T-0002).
  - Merged to `main`: `756a218` (includes hooks, CI skeleton, and PROGRESS handoff). Remote: `origin` → https://github.com/mayank-890/Tresuryyy.git
- Files allowed: `.pre-commit-config.yaml`, `.gitleaks.toml`, `.github/workflows/`, `tools/`
- Out of scope: any build step
- Tests required: pre-commit dry-run on a fixture secret; CI workflow linted (`actionlint` if available)

### T-0003: CMake superbuild, presets, vcpkg manifest, CTest
- Status: Done (on `main`; build verified via downstream merges through `b98a565`, 2026-10-29)
- Phase: 0 · Risk class: B · Depends on: T-0002
- Requirements: TI §3.1–§3.4
- Goal: Engine builds with each preset; empty targets exist; CTest runs; the watchdog link-restriction check exists (currently passes trivially).
- Acceptance criteria:
  - [x] Top-level `CMakeLists.txt` and `CMakePresets.json` with presets: `debug-asan`, `debug-tsan`, `debug-ubsan`, `release`, `bench`, `coverage`
  - [x] Options: `TRADING_ENABLE_GROWW=OFF`, `TRADING_BUILD_BINDINGS=OFF`, `TRADING_BUILD_WATCHDOG=ON`, `TRADING_ENABLE_LIVE_PROFILE=OFF`. Groww option verified OFF in every preset.
  - [x] `vcpkg.json` with GoogleTest only at this stage; baseline pinned
  - [x] Empty static libraries from TI §3.3 exist with one smoke test each
  - [x] `trader` and `watchdog` placeholder executables print version and exit 0
  - [x] `just build` and `just test` work on each preset
  - [x] A check script fails if `watchdog` links any `trading_strategy` or `trading_risk` target
- Evidence:
  - `CMakePresets.json` defines six configure/build/test presets; each sets `TRADING_ENABLE_GROWW=OFF` via hidden `vcpkg-base`.
  - `CMakeLists.txt` declares options with Groww fatal if enabled; `enable_testing()` before `add_subdirectory(engine)`.
  - `vcpkg.json` lists only `gtest`; `builtin-baseline` `15b12b99a227d51e21c86dffd40385439eac87a0`.
  - `engine/CMakeLists.txt` builds TI §3.3 libraries (Groww adapter omitted), `trader`, `watchdog`, and `*_tests` smoke executables (10 libraries).
  - `trader` / `watchdog` print `trading-engine 0.1.0 (placeholder)` and `trading-watchdog 0.1.0 (placeholder)`; CTest `trader_version` and `watchdog_version` pass.
  - `just check <preset>` passed for all six presets (2026-10-29 session).
  - `tools/check_watchdog_links.sh` inspects Ninja link line; passes on all preset build dirs.
- Files allowed: `engine/`, `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `justfile`, `tools/`
- Out of scope: real logic in any module; third-party libraries beyond GoogleTest (each needs an ADR)
- Tests required: CTest smoke tests per library; the watchdog link check runs in CI

### T-0004: Journal schema v1 and codegen
- Status: Done (merged `main` @ b98a565, 2026-10-29)
- Phase: 0 · Risk class: A · Depends on: T-0003
- Requirements: FR-JR-01, DESIGN §10, TI §5
- Goal: `schemas/journal/v1/records.yaml` defines the header and the record types in TI §5.2; a generator produces C++ and Python bindings; drift check in CI.
- Acceptance criteria:
  - [x] Header layout exactly as DESIGN §10.1 (40 bytes, little-endian); static assertion on size and offsets in C++
  - [x] Record type IDs as in TI §5.2
  - [x] `just codegen` generates `records.hpp`, `services/.../generated.py`, `research/.../generated.py`
  - [x] `just codegen` fails if output differs from committed files
  - [x] Round-trip test: encode and decode each record type in C++ and Python; results identical
  - [x] `schemas/journal/CHANGELOG.md` created with v1 entry
  - [ ] Human review required (Class A). Do not merge without it.
- Evidence:
  - `schemas/journal/v1/records.yaml`: 40-byte header fields at DESIGN §10.1 offsets; 18 record types with IDs `0x0001`–`0x0051` per TI §5.2.
  - `schemas/codegen/generate.py` + `load_schema.py` (stdlib YAML subset parser; no new dependencies).
  - Generated: `engine/include/trading/journal/generated/records.hpp` (`static_assert` size/offsets, CRC32C, encode/decode per type); matching Python modules under `services/` and `research/`.
  - `just codegen` / `just codegen-check` (drift exits non-zero on mismatch).
  - `schemas/tests/test_journal_round_trip.py` (22 unittest cases); `schemas/tests/journal_round_trip_test.cpp` (all payloads + header); C++/Python byte match on `SessionStart` sample.
  - `just check` passed on `debug-asan` (2026-10-29 session).
  - Merged to `main`: `b98a565` (`Merge T-0004: journal schema v1 and codegen`, 2026-10-29).
- Files allowed: `schemas/`, `engine/include/trading/journal/`, `services/` (generated only), `research/` (generated only), `justfile`
- Out of scope: writer or reader logic (T-0008 and Phase 3)
- Tests required: round-trip per record type; header size and offset tests; codegen drift test

### T-0005: Core primitives
- Status: Done (merged `main` @ ffaece3, 2026-10-29)
- Phase: 0 · Risk class: A · Depends on: T-0004
- Requirements: NFR-PERF-02, TI §4.1, DESIGN §4
- Goal: Clock, SPSC ring, object pool, lock-free log queue, thread-pinning helper. Tested under TSan and ASan.
- Acceptance criteria:
  - [x] `trading::core::Clock` wrapping `steady_clock` for mono and wall time, with no allocation
  - [x] `SpscRing<T, N>` with `alignas(64)` indices, explicit memory orders with comments, power-of-two capacity
  - [x] `ObjectPool<T>` preallocated, no allocation after construction, double-release detection in debug builds
  - [x] `LogQueue` lock-free producer side, drained by a dedicated thread (stub sink in this ticket)
  - [x] `pinThread(core)` helper with error result; warning counter on platforms without affinity support
  - [x] Allocation-counting test hook usable by later tests
  - [x] Tests: SPSC stress 10M (PR), wraparound, full/empty boundaries; TSan clean via `core_tests` binary
  - [ ] Human review required (Class A)
- Evidence:
  - Headers: `clock.hpp`, `spsc_ring.hpp`, `object_pool.hpp`, `log_queue.hpp`, `thread_pin.hpp`, `allocation_hook.hpp`, `result.hpp`.
  - Implementations compiled via `src/core/module.cpp` includes (`log_queue`, `thread_pin`, `allocation_hook`).
  - Unit tests: `engine/tests/unit/core/core_tests.cpp` (9 GTest cases); CTest target `core_tests` via `engine/tests/unit/core/CMakeLists.txt`.
  - `just check` (debug-asan, Darwin arm64): codegen + journal tests + CTest **13/13** + watchdog link check passed.
  - `just test-tsan` (Darwin arm64): CTest **13/13**; `core_tests` passed under TSan (~8.3s).
  - SPSC baseline: `engine/bench/core/baseline.txt` (~27.7M items/s, 5M items, `-O2` chrono bench; Google Benchmark not in vcpkg — Q-11).
  - Rename: `pinThread`, `pinThreadWarningCount` (2026-10-29 follow-up commit).
- Files allowed: `engine/include/trading/core/`, `engine/src/core/`, `engine/tests/unit/core/`, `engine/bench/core/`, plus operator amendment (2026-10-29): `engine/CMakeLists.txt`, `engine/tests/unit/core/CMakeLists.txt`, `justfile`
- Out of scope: any use of these primitives in strategy, risk, or broker code
- Tests required: unit; TSan stress; ASan; benchmark baseline recorded

### T-0006: PostgreSQL migrations and partition scripts
- Status: Done (merged `main` @ 080a18d, 2026-10-29)
- Phase: 0 · Risk class: A · Depends on: T-0002
- Requirements: FR-JR-03, DESIGN §11, TI §11
- Goal: Initial schema, roles, indexes, partition management; applied and rolled back in CI.
- Acceptance criteria:
  - [x] `docker/compose.yml` with PostgreSQL 16 bound to localhost (`127.0.0.1`; optional `DEV_DB_PORT`)
  - [x] dbmate migrations: schema `trading`; tables from DESIGN §11; roles `projector_writer`, `control_api_reader`, `report_reader` with least privilege
  - [x] Unique idempotency keys per ADR-0013: `(source_segment, source_sequence)` non-partitioned; `(source_segment, source_sequence, event_time)` partitioned
  - [x] Indexes per TI §11.1 (BRIN on event time for tick/event tables; B-tree on `client_order_id` for `orders`; B-tree on `(strategy_id, instrument_id, event_time)` for `fills`)
  - [x] Partition scripts create daily (3 days ahead) and monthly (2 months ahead) partitions; `drop_partition.sh` requires `ARCHIVE_VERIFIED=1`
  - [x] `projection_checkpoint` table
  - [x] CI job `migrations-apply-down`: apply → `db/tests/run_migration_tests.sh` → down → re-apply
  - [ ] Human review required (Class A)
- Evidence (2026-10-29, **Darwin arm64**):
  - `just migrate`: Applied `20260129100000_trading_initial.sql`
  - `just db-migrate-cycle`: rollback + re-apply clean
  - `just test-db`: daily/monthly partitions; **project same record twice** → one row (`wall_ns=1705326896789000000` → `event_time=2024-01-15 12:34:56.789+00`); privilege deny; drop guard
  - `just check`: codegen drift + journal tests + CTest **13/13** + watchdog link check
  - dbmate: `DBMATE_MIGRATIONS_TABLE=public.schema_migrations` (avoids collision with schema `trading`)
  - **Deterministic `event_time` (step 2):** Journal header `wall_ns` at offset 24 (`schemas/journal/v1/records.yaml`, DESIGN §10.1). No projector code in repo yet (`services/.../projector/` README only). Migration defines `event_time timestamptz NOT NULL` with **no DEFAULT** on projected tables; only `projection_checkpoint.updated_at` uses `DEFAULT now()` (checkpoint metadata, not journal projection). DESIGN §11 / TI §11.3 / ADR-0013 require projector to map `wall_ns` → `event_time`; no `now()` on projection path in code or schema defaults for `event_time`.
- Files allowed: `db/`, `docker/`, `.github/workflows/` (database job only), `justfile`
- Out of scope: projector code (Phase 1)
- Tests required: migration apply, down, re-apply; partition creation test; constraint tests (duplicate insert ignored by ON CONFLICT pattern)

### T-0007: Profiles, validator, promotion verifier, paper start
- Status: Done (merged `main`; feature commits `98f83bb`, `e7c052f`, `1f480e7`)
- Phase: 0 · Risk class: A · Depends on: T-0003
- Requirements: FR-OP-01, FR-OP-02 (scaffold only), TI §8, DESIGN §13
- Goal: Profiles are validated strictly; the engine starts in paper mode with no trading; live refuses without a valid promotion file.
- Acceptance criteria:
  - [x] `config/schema/profile.schema.json`, `promotion.schema.json`; `config/profiles/paper.toml`, `testnet.toml`, `live.toml` (live placeholders `TODO(operator)`)
  - [x] Validator rejects unknown keys and missing name (TC-CF-01, TC-CF-02) — `profile_tests`
  - [x] Profile content hash computed and printed at start (`config_hash=`, `journal_config_hash_recorded=`)
  - [x] Promotion verifier (JSON + Ed25519 via OpenSSL); runtime test keys only (TC-CF-03–TC-CF-06, valid signed promotion pass)
  - [x] Live without promotion file fails (TC-CF-03); logs `event=AL-13-equiv`
  - [x] Groww adapter with `TRADING_ENABLE_GROWW=OFF` fails (TC-CF-08)
  - [x] Missing secret fails without printing value (TC-CF-09)
  - [x] `trader --profile paper --config-root config --smoke` → `RUNNING (no trading logic)`; SIGTERM path in `main` (manual / future UI test)
  - [ ] Human review required (Class A)
- Evidence (2026-10-29, **Darwin arm64**):
  - `just check`: CTest **14/14** (`profile_tests`, `trader_version` with secret env)
  - `just test-tsan`: CTest **14/14**
  - ADR-0014 Proposed (`tomlplusplus`, `nlohmann-json`, `openssl` in `vcpkg.json`); **Q-13** open for operator acceptance + CMake scope
  - Re-verified on branch tip `15aa4d6` / `e7c052f` (same green results, 2026-10-29 evening session)
  - Merged to `main`: `a3019bf` (`Merge T-0007: profiles, validator, promotion verifier, paper trader`, 2026-10-29)
- Files allowed: `config/`, `engine/src/apps/trader/`, `engine/src/calendar/` (stub only), `engine/tests/`, `tools/`
- Out of scope: strategies, orders, broker adapters
- Tests required: TC-CF-01 to TC-CF-09 as applicable at Phase 0

### T-0008: Journal inspector
- Status: Done (merged `main` @ bcb67f9, 2026-10-29)
- Phase: 0 · Risk class: C · Depends on: T-0004, T-0005
- Requirements: FR-JR-02, TC-JR-02, TC-JR-10
- Goal: Read-only tool that validates journal segments, reports torn tails, and pretty-prints records.
- Acceptance criteria:
  - [x] Reads v1 segments; validates CRC and sequence continuity
  - [x] Detects a torn tail and reports it without modifying the file (`--repair` is NOT implemented in this ticket)
  - [x] Mid-file CRC failure reported as fatal with byte offset
  - [x] Filtering by record type and sequence range
  - [x] Fixture set in `tests/fixtures/compat/v1/` (small, synthetic) covering normal, torn-tail, and CRC-corrupt cases
- Evidence (2026-10-29, **Darwin arm64**):
  - CLI: `python3 tools/journal_inspect/__main__.py tests/fixtures/compat/v1/normal.segment`
  - Tests: `bash tools/journal_inspect/run_tests.sh` — 6 unittest cases (normal, torn AL-14, CRC fatal offset, filters, SHA-256 unchanged)
  - `just check`: CTest **14/14**, journal schema tests, watchdog link check (on branch rebased with T-0007 on `main`)
  - Re-verified 2026-10-29 after merge `main` into branch
  - Merged to `main`: `bcb67f9` (`Merge T-0008: journal inspector`, 2026-10-29)
- Files allowed: `tools/journal_inspect/`, `tests/fixtures/compat/v1/`
- Out of scope: writer; repair; any change to the journal format
- Tests required: fixture tests for each case; read-only check (file hash unchanged after run)

### T-0009: Python research package skeleton
- Status: Done (merged `main` @ f6bbe81, 2026-10-29)
- Phase: 0 · Risk class: B · Depends on: T-0002
- Requirements: FR-RS-01 (scaffold), TI §4.2
- Goal: `research/` is a uv project with lint, types, tests, and a `tradelab.engine` stub.
- Acceptance criteria:
  - [x] `research/pyproject.toml`, `uv.lock`, `.python-version` (3.12)
  - [x] `ruff` and `mypy --strict` configured and passing on the skeleton
  - [x] `tradelab.engine` module exists as a stub that raises `NotImplementedError` with a message referencing ADR-0005
  - [x] pytest runs with one smoke test
- Evidence (2026-10-29, **Darwin arm64**):
  - Commits: `8971057` (uv project + stub), `fa27656` (just research targets), `91f5695` / `58563d5` (handoff); merge `f6bbe81`.
  - `just research-lint`: ruff check/format, mypy strict on `src/tradelab/engine`
  - `just research-test`: pytest **1 passed**
  - `just check`: full gate including research + CTest **14/14** (re-verified on `main` @ f6bbe81)
- Files allowed: `research/`, `justfile`
- Out of scope: data ingestion, backtesting, pybind11 build
- Tests required: smoke test; lint and types clean

### T-0010: Frontend skeleton with smoke test
- Status: Done (merged `main` @ 8251a18, 2026-10-29)
- Phase: 0 · Risk class: B · Depends on: T-0002
- Requirements: FR-UI-01 (scaffold), TI §4.3
- Goal: Static dashboard shell with module structure, a reducer, and a Playwright smoke test. No live data.
- Acceptance criteria:
  - [x] `ui/index.html`, `ui/css/`, `ui/js/state/`, `ui/js/views/`, `ui/js/transport/`
  - [x] Pure reducer with unit tests (Node test runner or Vitest; choice recorded in an ADR if a new dependency is needed)
  - [x] Dashboard shell shows the panels in USER_OPERATIONS §5.1 with placeholder "no data" state
  - [x] Playwright smoke test loads the page and checks every panel heading exists
  - [x] No `innerHTML` with data; lint rule enforces this if a linter is configured
- Evidence (2026-10-29, **Darwin arm64**):
  - `5899d71`: ES module shell, `dashboardReducer` + store, `textContent` rendering, poll stub.
  - `npm run test:unit` in `ui/`: **4 passed** (reducer + innerHTML guard); Node `node:test` (no extra test framework ADR).
  - `just ui-test` / `npm run test:e2e`: **1 passed** (all ten §5.1 panel `<h2>` headings).
  - `just check`: ui-unit + CTest **14/14** + prior gates green at commit.
- Files allowed: `ui/`, `justfile`
- Out of scope: control API; live data; kill controls (Phase 5)
- Tests required: reducer unit tests; Playwright smoke

### T-0011: Calendar and instrument master loader
- Status: Done (merged `main` @ 8726a22, 2026-10-29)
- Phase: 0 · Risk class: B · Depends on: T-0007
- Requirements: FR-MD-04 (session data), TI §8, DESIGN §8.3
- Goal: Load and validate trading calendars and instrument master files. No real operator data.
- Acceptance criteria:
  - [x] Calendar format supports sessions per product, holidays, early closes, and maintenance windows
  - [x] Instrument master format: symbol, venue, lot size, tick size, expiry, strike, option type, underlying
  - [x] Validator rejects overlapping sessions, zero or negative tick or lot sizes, and duplicate symbols
  - [x] Sample files use synthetic values and are marked `# SAMPLE — NOT FOR TRADING`
  - [x] Loader exposes lookup by symbol and session query by time, with a fixed-clock test
- Evidence (2026-10-29, **Darwin arm64**):
  - `d0a2697`: TOML loaders (`calendar_load`, `instrument_load`), validators, `query_session` with holidays/early close/maintenance.
  - Sample configs: `config/calendars/crypto_24x7.toml`, `config/instruments/delta_crypto.toml` (referenced by paper profile).
  - Tests: `engine/tests/unit/profile/calendar_instrument_tests.cpp` (overlap reject, boundary session query, duplicate symbol, lookup).
  - `just check`: CTest **14/14** including expanded `profile_tests`.
- Files allowed: `engine/src/calendar/`, `engine/src/instruments/`, `config/calendars/`, `config/instruments/`, `engine/tests/`
- Out of scope: real instrument data; roll logic (Phase 2); session behavior in strategies
- Tests required: validator tests; lookup tests; session query at boundaries

## Phase 1 Backlog

### T-0101: Delta historical OHLCV ingestion + data quality
- Status: Done
- Phase: 1 · Risk class: B · Depends on: Phase 0 exit; T-0009 (research package)
- Goal: Historical OHLCV loader (Delta public REST), Parquet under external data root, gap/outlier checks, roll-adjust stub, CLI backfill, fixture tests (no network in unit tests).
- Acceptance criteria:
  - [x] ADR-0015 Accepted; `pyarrow` in `research/pyproject.toml` + `uv.lock` only
  - [x] Delta public REST candle loader with paging (`tradelab.data.ingest.delta_rest`)
  - [x] Normalized OHLCV Parquet under `TRADELAB_DATA_ROOT` hive layout (`tradelab.data.store`)
  - [x] Gap detection and basic outlier flags (`tradelab.data.quality.checks`)
  - [x] Documented futures roll-adjustment stub (`tradelab.data.adjust.futures_roll`)
  - [x] CLI backfill date range (`tradelab-ohlcv-backfill`)
  - [x] Unit tests against `tests/fixtures/delta/` only (no live network)
- Evidence (2026-10-30, **Darwin arm64**):
  - `just check`: codegen, journal **22/22**, research ruff/mypy/pytest **11/11**, UI **4/4**, CTest **14/14**, watchdog link check pass.
  - Fixture: `tests/fixtures/delta/candles_btcusd_1m.json`; gap at missing 1m bar exercised in `research/tests/unit/test_delta_ohlcv.py`.
  - Parquet path: `{TRADELAB_DATA_ROOT}/ohlcv/delta/symbol={SYMBOL}/resolution={RES}/bars.parquet`.
- Data licensing (record before merge): Delta Exchange API/historical data use is subject to [Delta Exchange terms](https://www.delta.exchange/legal/terms-of-use) and API documentation; **TODO(operator):** confirm redistribution/storage rights for downloaded candles before production backfills.
- Files allowed: `research/`, `services/ingestion/`, `tests/`, `tools/`, `PROGRESS.md`, `docs/` (licensing note only)
- Merged to `main`: `de58b1c` (2026-10-30)

### T-0102: Live tick capture to journal (recording only)
- Status: Done
- Phase: 1 · Risk class: B · Depends on: T-0101; journal v1 schema (T-0004)
- Goal: Delta public market-data recorder → normalized `MarketQuote` / `MarketTrade` / `FeedState` in v1 journal; gap/stale health; clean shutdown with fsync; mock-feed tests (no CI network).
- Acceptance criteria:
  - [x] Journal segment writer (Python service + C++ `SegmentWriter`) using v1 codegen only
  - [x] Delta public message normalization (`engine/.../delta_normalize`, `services/.../delta_normalize.py`)
  - [x] Recording session: SessionStart → market records → SessionEnd; flush on shutdown
  - [x] Feed health: sequence gap → `FeedState`; stale tracker (configurable threshold)
  - [x] CLI `python -m tradelab_services.ingestion` mock mode (no order APIs linked)
  - [x] Tests: `tests/unit/ingestion/` + CTest journal/marketdata; journal inspect **OK**
- Evidence (2026-10-30, **Darwin arm64**):
  - `just check`: services-test **2/2**, research **11/11**, CTest **16/16** (adds `trading_journal_writer_tests`, `trading_marketdata_delta_tests`).
  - Fixtures: `tests/fixtures/delta/public_feed.ndjson`, `tests/fixtures/ingestion/recording_instruments.toml`.
- Open questions (T-0102):
  - **Q-16:** Production instrument list and `instrument_id` map for live Delta symbols — **TODO(operator)**
  - **Q-17:** Journal segment retention path and rotation policy under `TRADELAB_JOURNAL_ROOT` — **TODO(operator)**
  - **Q-18:** Delta live public feed licensing / terms for continuous capture — **TODO(operator)** ([terms](https://www.delta.exchange/legal/terms-of-use))
- Live network: CI uses **mock** / **mock-ws** only; `--mode live` wired (ADR-0016) — production gated on Q-16/Q-18.
- Files allowed: `services/ingestion/`, `engine/src/journal/`, `engine/src/marketdata/`, `tests/`, `tools/`, `PROGRESS.md`, `docs/` (notes only). **Amendment:** `engine/CMakeLists.txt` (`trading_add_module` multi-source + test targets), `justfile` (`services-test`).
- Merged to `main`: `72e7380` (2026-10-30)

### T-0103: Journal data-quality report
- Status: Done
- Phase: 1 · Risk class: C · Depends on: T-0008 (inspector); T-0102 (capture fixtures)
- Goal: Read-only DQ report on v1 journal segments: sequence gaps, stale periods, CRC/torn-tail (via inspector), per-instrument tick stats.
- Acceptance criteria:
  - [x] CLI `python -m journal_dq_report` with text and JSON output
  - [x] Reuses T-0008 inspector for strict CRC/torn-tail; lenient scan for gap statistics
  - [x] Configurable stale threshold (`--stale-threshold-ms`)
  - [x] Per-instrument quote/trade counts and max inter-event gap
  - [x] Tests on normal, torn_tail, crc_corrupt, sequence_gap, market_sample fixtures
- Evidence (2026-10-30, **Darwin arm64**):
  - `just journal-dq-test`: **6/6**; wired in `just check`.
  - Fixtures: `tests/fixtures/compat/v1/sequence_gap.segment`, `market_sample.segment` (via `build_fixtures.py`).
- Open questions (T-0103):
  - **Q-19:** Default stale threshold for production DQ reports (CLI default 5000 ms) — **TODO(operator)**
  - **Q-20:** How DQ reports interact with journal retention (Q-17) and Phase 1 exit criteria — **TODO(operator)**
- Files allowed: `tools/`, `services/` (read-only analysis), `tests/`, `PROGRESS.md`, `docs/` (notes only). **Amendment:** `tools/journal_inspect/build_fixtures.py` (new fixtures).
- Merged to `main`: `049ff86` (2026-10-30)

### T-0104: Instrument master enrichment (Q-16)
- Status: Done
- Phase: 1 · Risk class: B · Depends on: T-0011; T-0102 (recording symbols)
- Goal: Map Delta/journal symbols to `instrument_id`, lot/tick sizes; enrichment CLI; validation reused from T-0011.
- Acceptance criteria:
  - [x] Optional `instrument_id` in instrument master TOML + duplicate/positive validation
  - [x] Merge seed + patch masters; symbol list enrichment report (missing symbol / missing id)
  - [x] `instrument_enrich` CLI (`--seed`, `--patch`, `--symbols`, `--output`)
  - [x] Sample recording map `config/instruments/delta_recording_ids.toml`
  - [x] Tests in `instrument_enrich_tests` + CTest `instrument_enrich_cli`
- Evidence (2026-10-30, **Darwin arm64**):
  - `just check`: CTest **17/17** (includes `profile_tests` +4 enrich cases, `instrument_enrich_cli`).
  - Fixture symbols: `tests/fixtures/instruments/journal_symbols.txt`.
- Operator TODO (Q-16): **Live Delta symbol list** and authoritative **lot_size / tick_size** sources — repo uses SAMPLE values only.
- Files allowed: `config/instruments/`, `engine/src/instruments/`, `tools/`, `tests/`, `PROGRESS.md`, `docs/` (notes only). **Amendment:** `engine/tests/unit/profile/CMakeLists.txt` (CLI test target).
- Merged to `main`: `dbeacdb` (2026-10-30)

### T-0105: Journal retention / rotation (Q-17)
- Status: Done
- Phase: 1 · Risk class: B · Depends on: T-0102 (journal writer)
- Goal: Rotate journal segments on size/time; immutable closed segments with footer; retention with archive guard; ops CLI.
- Acceptance criteria:
  - [x] `RotatingJournalWriter` — size/time rotation, sequence continuity, shutdown footer flush
  - [x] `TJNF` segment footer (record count, last sequence, body CRC)
  - [x] Retention by closed-segment count and/or age; refuse without `ARCHIVE_VERIFIED=1`
  - [x] `journal_retention` CLI (`status`, `apply`) + Python status mirror (`python -m journal_retention`)
  - [x] Tests: `trading_journal_rotation_tests`, `journal_retention_cli`, `just journal-retention-test`
- Evidence (2026-10-30, **Darwin arm64**):
  - `just check`: CTest **19/19**; journal-retention pytest **2/2**.
- Operator TODO (Q-17): **Production rotation thresholds**, **archive destination**, and wiring capture to `RotatingJournalWriter` — defaults only in code.
- Files allowed: `engine/src/journal/`, `tools/`, `services/ingestion/` (optional), `tests/`, `PROGRESS.md`, `docs/` (notes). **Amendment:** `engine/CMakeLists.txt`, `justfile`.
- Merged to `main`: `cddb2e9` (2026-10-30)

### T-0106: Live WebSocket tick capture (Delta public feed)
- Status: Done
- Phase: 1 · Risk class: B · Depends on: T-0105, ADR-0016 Accepted (`aa438de`)
- Goal: Delta public WS → normalize → Python `RotatingJournalWriter`; health/gaps/stale; reconnect+backoff; mock-ws CI.
- Acceptance criteria:
  - [x] ADR-0016 Accepted; `websockets` pinned in research uv lock
  - [x] `--mode live|mock|mock-ws` CLI; empty instrument list → no connect (code 2)
  - [x] Rotating journal segments with TJNF footer on shutdown
  - [x] Reconnect backoff + disconnect `FeedState`; stale/gap tracking preserved
  - [x] Tests: ingestion **5/5** (mock, mock-ws, empty list, backoff) — no live network in CI
- Evidence (2026-10-30, **Darwin arm64**): `just check` green; services-test **5/5**.
- Operator TODO: Q-16 live symbols; Q-18 licensing before continuous live capture; Q-21 subscribe channel confirmation.
- Files allowed: `services/ingestion/`, `engine/src/marketdata/`, `engine/src/journal/` (integration via services), `tests/`, `tools/`, `PROGRESS.md`, `docs/`. **Amendment:** `research/pyproject.toml`, `justfile`.
- Merged to `main`: `67ad621` (2026-10-30)

### T-0107: Journal recovery smoke (recording path, FR-JR-02)
- Status: Done
- Phase: 1 · Risk class: C · Depends on: T-0106; T-0008 (inspector)
- Requirements: FR-JR-02 (TC-JR-02 / TC-JR-08 spirit; recording path only)
- Goal: Validate crash-safe recording: torn-tail detection (AL-14), copy-safe truncate to the valid prefix; repeated kill-during-write smoke on synthetic segments only — not the engine order path.
- Acceptance criteria:
  - [x] Recovery CLI truncates a torn tail after the inspector reports `TORN_TAIL`; prior records intact
  - [x] Automated test: 100 synthetic write/tear/recover cycles; inspector OK on the recovered prefix
  - [x] Operator steps in `tools/journal_recover/README.md` and `docs/runbooks/RB-10-torn-journal-tail.md`; no live trading paths
- Scope decision (operator, 2026-10-31, “truncate safely”): copy-safe truncate. Dry-run is the default. `--apply` fsyncs the original bytes to `<segment>.torn`, re-inspects the prefix, then atomically replaces the segment. `FATAL` (CRC, sequence gap, bad magic) is never truncated. A valid `TJNF` footer is not stripped. The inspector remains read-only.
- Evidence (2026-10-31, **Darwin arm64**): `just check` green — codegen, journal schema **22/22**, research **11/11**, services **5/5**, journal-dq **6/6**, journal-retention **2/2**, journal-recovery **9/9** (100 kill-during-write cycles), UI **4/4**, CTest **19/19**, watchdog link check.
- Files allowed: `tools/journal_inspect/`, `tools/`, `tests/`, `PROGRESS.md`, `docs/` (notes only). **Amendment:** `justfile` (`journal-recovery-test` wired into `just check`) so the new tests run in the commit gate.
- Out of scope: order path, risk, broker, engine startup recovery, live trading
- Merged to `main`: `d773db8` (fast-forward of `feat/T-0107-journal-recovery-smoke`; feature `70378ba`)

### T-0108: Journal-to-PostgreSQL projector stub (idempotent)
- Status: Done
- Phase: 1 · Risk class: B · Depends on: T-0006 (schema); T-0004 (journal records). Marked Ready in the implementation session, then implemented.
- Requirements: FR-JR-03
- Goal: Basic projector that reads journal records and writes them idempotently to PostgreSQL. A PostgreSQL outage does not stop recording.
- Acceptance criteria:
  - [x] Projector reads journal segments and projects core record types
  - [x] Idempotent writes (re-running does not duplicate)
  - [x] Handles missing/out-of-order sequences safely
  - [x] Basic tests (unit + one integration against local Postgres)
  - [x] Documented how to run it
- Core rows: `MarketQuote` / `MarketTrade` → `market_ticks`; `OrderIntent` → `orders`; `OrderEvent` → `order_events` plus a newer-sequence status update; `RiskRejected` → `risk_events`; `ControlCommand` → `control_commands`; `SessionStart` → `config_versions`. `event_time` is `wall_ns`, not `now()`.
- Not projected: `FillEvent`, `PositionSnapshot` (Q-22). No new migration. No new Python driver (Q-23); client is `psql`.
- Evidence (2026-10-31, **Darwin arm64**): `just check` green on `3b5fb1f` — codegen, journal schema **22/22**, research **11/11**, services **5/5**, journal-dq **6/6**, journal-retention **2/2**, projector unit **9/9**, UI **4/4**, CTest **19/19**, watchdog link check. `just projector-integration` **1/1** (re-project → 2 tick rows, 1 order row, `event_time` = `2024-01-15 13:54:56.789+00`).
- Files allowed: `services/src/tradelab_services/projector/`, `db/migrations/` (new migration only — none added), `services/tests/`, `PROGRESS.md`, `justfile`.
- Out of scope: order path, risk limits, live trading, UI
- Run: `PYTHONPATH=services/src:tools:tools/journal_inspect python3 -m tradelab_services.projector --segment <file>` (`--dry-run` does not connect). `just projector-integration` needs `DATABASE_URL` / `just migrate`.
- Merged to `main`: this merge (`Merge T-0108: projector stub`; feature `3b5fb1f`)

## Phase 1 Backlog (legacy note)

Tickets above supersede “Not Yet Ready” once Phase 0 exit is recorded in `docs/reviews/phase-0.md`.

## Open Questions

| ID | Question | Blocks | Status |
|---|---|---|---|
| Q-1 | Does Groww offer an official trading API? | G-LIVE-1; Groww adapter | Open |
| Q-2 | Algorithmic-trading registration path for this account and these venues | G-LIVE-2 | Open |
| Q-3 | Historical data source for Indian derivatives and commodities | Phase 2 Indian instruments | Open |
| Q-4 | Capital allocation and risk budget for Phase 7 | G-LIVE-5 | Open |
| Q-5 | Logging library (ADR-0011) | T-0005 (may proceed with the custom sink; decision needed before Phase 3) | Open — agent proposes options, operator decides |
| Q-6 | Tool-switch notification: how the operator signals Codex limit reached | Process only | Open (operator preference) |
| Q-7 | Confirm `.gitignore` should anchor `/data/` and `/secrets/` at the repository root. Unanchored `data/` would ignore `research/src/tradelab/data/`. Unanchored `secrets/` would ignore `deploy/secrets/` (layout documentation, no values). T-0001 used the anchored form so those skeleton paths stay tracked. | none for T-0001 review | Open |
| Q-8 | Add `.vcpkg/` to `.gitignore` (local vcpkg clone from `just setup-vcpkg`). | none | Done — merged to `main` at `e5c43aa` (2026-10-29) |
| Q-9 | T-0004 branch stacks `feat/T-0003-cmake-superbuild` until T-0003 is on `main`. Confirm PR base and merge order with operator. | T-0004 review | Done — T-0003/T-0004 on `main` (`b98a565`); PROGRESS updated 2026-10-29 |
| Q-10 | Wire `core_tests` into CTest and add `just test-tsan`. | T-0005 CI | Done — merged in `ffaece3` |
| Q-12 | Partitioned-table unique keys include `event_time` (option A). | T-0006 | Done — operator approved 2026-10-29; ADR-0013 Accepted |
| Q-13 | Accept ADR-0014 (`tomlplusplus`, `nlohmann-json`, `openssl` in `vcpkg.json`) and `engine/CMakeLists.txt` / `vcpkg.json` edits for T-0007. | T-0007 merge | Open — implemented pending operator sign-off |
| Q-11 | Add Google Benchmark to `vcpkg.json` (ADR) for nightly SPSC regression vs `engine/bench/core/baseline.txt`. T-0005 used std::chrono bench only. | Phase 3 perf gates | Open |
| Q-14 | Accept `engine/CMakeLists.txt` amendment for T-0011 (`trading_profile` links `trading_core`, exposes `engine/include`). | T-0011 merge | Open — required for loaders |
| Q-15 | Accept **ADR-0015** (pyarrow, research Parquet only) to clear B-0101-3. | T-0101 | Done — Accepted on `main` @ `ac6c987` (2026-10-30) |
| Q-16 | Live Delta recording instrument list and `instrument_id` mapping | T-0102 live | Open — operator must supply live symbols and lot/tick sources (`instrument_enrich` on `main`) |
| Q-17 | Journal segment retention / rotation under `TRADELAB_JOURNAL_ROOT` | T-0102 ops | Open — tooling on `main`; operator sets limits and archive path |
| Q-18 | Delta live public feed capture licensing | T-0106 live prod | Open — **TODO(operator)** before continuous `--mode live` |
| Q-21 | Delta public WS subscribe channels / symbol naming for production | T-0106 live prod | Open — repo uses `v2_ticker` + instrument TOML symbols (SAMPLE) |
| B-0106-1 | Accept ADR-0016 | T-0106 | Done — `aa438de` on `main` (2026-10-30) |
| Q-19 | Default stale threshold for journal DQ reports | T-0103 ops | Open — CLI default 5000 ms |
| Q-20 | DQ reporting vs journal retention (Q-17) / Phase 1 exit | T-0103 | Open |
| Q-22 | `FillEvent` has no `strategy_id` or `instrument_id`; `PositionSnapshot` has no `strategy_id`. `fills` and `position_snapshots` require those columns. Projector does not invent them. | Fuller FR-JR-03 projection | Open — **TODO(operator)** |
| Q-23 | Python PostgreSQL driver for the projector (psycopg or similar) | Replace the `psql` stub | Open — needs an ADR before any new dependency |

## Decisions Log

| Date | Decision | Reference |
|---|---|---|
| 2026-10-31 | Projector stub uses `psql` and `INSERT ... ON CONFLICT`. No new dependency and no new migration. Database failure does not modify the journal. | T-0108; Q-23 |
| 2026-10-31 | Torn-tail recovery is copy-safe truncate: `.torn` backup, then atomic prefix replace. FATAL scans and valid TJNF footers are not truncated. Inspector stays read-only. | T-0107 |
| 2026-10-30 | `websockets` for services ingestion public feed capture only (T-0106) | ADR-0016 |
| 2026-10-30 | pyarrow for research-side Parquet I/O only (T-0101) | ADR-0015 |
| 2026-10-29 | Partitioned projection unique keys include `event_time` from journal `wall_ns` | ADR-0013 |
| 2026-10-25 | Codex is primary; Cursor is fallback; both read AGENTS.md | PROCESS §1.1, §7.6 |
| 2026-10-25 | Phase 0 scope is limited to tickets T-0001 to T-0011 | PROCESS §6.2 |
| 2026-10-25 | Groww adapter remains behind OFF flag; mock only until G-LIVE-1 and G-LIVE-2 | PRD §12; ADR-0004 |
| 2026-10-29 | `.gitignore` anchors `/data/` and `/secrets/` at the repository root | Q-7; T-0001 |
| 2026-10-29 | T-0001 document commit is on `main`; skeleton commit is on `chore/T-0001-repo-skeleton` | PROCESS §4.1 |
| 2026-10-29 | `.gitleaks.toml` uses default rules via `[extend] useDefault = true` | T-0002 |
| 2026-10-29 | T-0001 merged to `main` (`09cfc893`) at operator request | T-0002 unblocked |

## Session Log

Append one entry per session. Newest first.

| Date (IST) | Tool | Ticket | Commits | Result | Notes |
|---|---|---|---|---|---|
| 2026-10-31 | cursor | T-0108 | merge `Merge T-0108: projector stub` (feature `3b5fb1f`) | Done | Idempotent projector stub on `main`. |
| 2026-10-31 | cursor | T-0107 | `d773db8` | Done | Fast-forward onto `main` (feature `70378ba`). |
| 2026-10-31 | cursor | T-0108 | `3b5fb1f` | In review | Idempotent projector stub before merge. |
| 2026-10-31 | cursor | T-0107 | `70378ba` | In review | Copy-safe torn-tail recovery before merge. |
| 2026-10-31 | cursor | — | — | — | Sync PROGRESS: T-0106 Done on `main`; T-0107 backlog draft. |
| 2026-10-30 | cursor | T-0106 | `67ad621` merge | Done | Live WS capture on `main`. |
| 2026-10-30 | cursor | T-0106 | `bbfa29b` | In review | Live WS capture + rotating journal; mock-ws CI. |
| 2026-10-30 | operator | ADR-0016 | `aa438de` | Accepted | websockets for ingestion. |
| 2026-10-30 | cursor | T-0105 | `cddb2e9` merge | Done | Journal retention on `main`. |
| 2026-10-30 | cursor | T-0105 | `6da738c` | In review | Journal rotation/retention + journal_retention CLI. |
| 2026-10-30 | cursor | T-0104 | `dbeacdb` merge | Done | Instrument enrichment on `main`. |
| 2026-10-30 | cursor | T-0104 | `42b00e1` | In review | Instrument enrichment + instrument_enrich CLI. |
| 2026-10-30 | cursor | T-0103 | `049ff86` merge | Done | Journal DQ on `main`. |
| 2026-10-30 | cursor | T-0103 | (feature commit) | In review | Journal DQ report CLI + fixtures. |
| 2026-10-30 | cursor | T-0102 | `72e7380` merge | Done | Tick capture on `main`. |
| 2026-10-30 | cursor | T-0102 | (feature commit) | In review | Tick recorder → journal; mock feed tests; no trading. |
| 2026-10-30 | cursor | T-0101 | `de58b1c` merge | Done | Delta OHLCV on `main`. |
| 2026-10-30 | cursor | T-0101 | (feature commits) | In review | Delta OHLCV ingest + Parquet + DQ + CLI; ADR-0015 Accepted on main. |
| 2026-10-29 | cursor | Phase 0 exit | (docs commit) | Pending operator | `docs/reviews/phase-0.md`, ADR-0015 Proposed; T-0101 not started. |
| 2026-10-29 | cursor | T-0011 | `8726a22` (merge) | Done | Calendar/instrument loaders on `main`. |
| 2026-10-29 | cursor | T-0011 | `d0a2697` | In review | Calendar/instrument TOML loaders, sample configs, profile_tests coverage. |
| 2026-10-29 | cursor | T-0010 | `8251a18` (merge) | Done | Frontend skeleton on `main`. |
| 2026-10-29 | cursor | T-0010 | `5899d71` | In review | Dashboard §5.1 panels, reducer unit tests, Playwright smoke; `just ui-test-ci` for CI browsers. |
| 2026-10-29 | cursor | T-0009 | `f6bbe81` (merge), PROGRESS | Done | Re-verified `just check` on `main`; PROGRESS synced post-merge. |
| 2026-10-29 | cursor | T-0009 | `8971057`, `fa27656` | In review | uv project, engine ADR-0005 stub, ruff/mypy/pytest wired in `just check`. |
| 2026-10-29 | cursor | T-0008 | `bcb67f9` | Done | Merged journal inspector to `main`. |
| 2026-10-29 | cursor | T-0008 | `e5dbd38`, `e9d68a3` | In review | Journal inspector; merged `main` (T-0007); `just check` 14/14 + inspector tests 6/6. |
| 2026-10-29 | cursor | T-0007 | `a3019bf` | Done | Merge to `main`; PROGRESS conflict resolved; `just check` 14/14 post-merge. |
| 2026-10-29 | cursor | T-0101 | — | Blocked | Phase 0 gate; Blockers B-0101-1..3 (T-0007 now unblocks B-0101-1 partially). |
| 2026-10-29 | cursor | T-0007 | `e7c052f`, `98f83bb` | In review → Done | Re-ran `just check` + `test-tsan` 14/14; marked T-0003/T-0004 Done on `main`. |
| 2026-10-29 | cursor | T-0005 | `e0f2d4d`, `3d11e6c` | In review | Core Clock, SpscRing, ObjectPool, LogQueue, pin_thread, allocation hook; 10M SPSC stress; TSan clean. |
| 2026-10-29 | cursor | T-0004 | `890a6c5`, `574d14c` | In review | Journal v1 schema, codegen drift check, C++/Python round-trip tests; branch includes T-0003 build. |
| 2026-10-29 | cursor | T-0003 | `4e544e6`, PROGRESS commit | In review | CMake superbuild, six presets, vcpkg gtest, empty engine targets, watchdog link check. |
| 2026-10-29 | cursor | T-0002 | `756a218` on `main` | Done | Hooks + CI skeleton; pushed to GitHub `origin`. |
| 2026-10-29 | cursor | T-0002 | `09cfc89` (merge T-0001), `e3bf1dc`, PROGRESS commit | Done | Merged T-0001 to `main`. T-0002 hooks + CI skeleton. Pre-commit demo blocked synthetic secret. |
| 2026-10-29 | cursor | T-0001 | `8086b9f`, `a9defd7`, and the commit that adds this row | Done | Repo was empty. `git` 2.54.0. `user.name` and `user.email` were set. No remote configured. `just` is not on PATH and was not installed. `tools/check_repo_skeleton.sh`: `skeleton check passed (287 paths)`. PR not opened: no remote. |
| 2026-10-25 | human | — | — | Kickoff documents created | Starting point for T-0001 |

## Blockers

| ID | Ticket | Reason | Unblock |
|---|---|---|---|
| B-0101-1 | T-0101 | **Process gate:** Phase 0 exit note needs operator sign-off. | Operator confirms `docs/reviews/phase-0.md`. |
| B-0101-2 | T-0101 | **Scaffold:** Research uv/pytest skeleton (T-0009). | Done — T-0009 on `main` @ f6bbe81. |
| B-0101-3 | T-0101 | **Dependencies:** pyarrow for research Parquet I/O. | Done — ADR-0015 Accepted `ac6c987`; pyarrow on feature branch. |
