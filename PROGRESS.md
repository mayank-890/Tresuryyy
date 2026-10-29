# PROGRESS.md

Single source of truth for the current state of work. Read this first in every session. Update it at every checkpoint.

## Handoff
- Updated: 2026-10-29T16:45:00+0530 by cursor
- Active ticket: T-0008
- Branch: feat/T-0008-journal-inspector
- Last green commit: b554f34 — `T-0008: fix handoff commit SHA in PROGRESS`
- Working tree: clean (after commit)
- Tests passing at last commit: **Darwin arm64** — `just check` (CTest 13/13); `bash tools/journal_inspect/run_tests.sh` (6/6)
- Next step (exact): Operator review T-0008; merge to `main`. Do not start T-0009 until T-0008 is Done on `main`. Note: T-0007 remains In review on branch `feat/T-0007-profiles-promotion` (not on `main`).
- Files touched this ticket: core headers/sources/tests/bench; `engine/CMakeLists.txt`, `engine/tests/unit/core/CMakeLists.txt`, `justfile` (operator scope amendment 2026-10-29).
- Decisions made this session (not yet in ADR): Renamed `pinThread` / `pinThreadWarningCount` (TI §4.1 camelCase). macOS `pinThread` returns `Unsupported`; **Linux CI is authority for affinity success path**.
- Open questions: Q-11; Q-1 through Q-9 unchanged. Q-10 resolved.
- Notes for the next tool: `run_core_tests.sh` retained as auxiliary; primary path is CTest `core_tests`.

## Current Phase

**Phase 0: Foundation (weeks 1–2).** Exit criteria: PROCESS_PROJECT_MANAGEMENT.md §6.1, row 0.

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
- Status: In review
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
- Status: In review
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
  - Branch stacks T-0003 build (`feat/T-0003-cmake-superbuild` fast-forward) until T-0003 is on `main`.
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
- Status: Backlog
- Phase: 0 · Risk class: A · Depends on: T-0003
- Requirements: FR-OP-01, FR-OP-02 (scaffold only), TI §8, DESIGN §13
- Goal: Profiles are validated strictly; the engine starts in paper mode with no trading; live refuses without a valid promotion file.
- Acceptance criteria:
  - [ ] `config/schema/profile.schema.json` and `config/profiles/paper.toml`, `testnet.toml`, `live.toml` (live has no active values; placeholders marked `TODO(operator)`)
  - [ ] Validator rejects unknown keys and missing name (TC-CF-01, TC-CF-02)
  - [ ] Profile content hash computed and printed at start
  - [ ] Promotion verifier implemented against a documented format; tests use test keys only, never real keys (TC-CF-03 to TC-CF-06)
  - [ ] Live profile refuses to start without a valid promotion file (TC-CF-03)
  - [ ] Groww adapter referenced while flag OFF fails startup (TC-CF-08)
  - [ ] Secret references resolved by name; a missing secret fails startup without printing values (TC-CF-09)
  - [ ] `trader --profile paper` starts, logs `RUNNING (no trading logic)`, and exits cleanly on SIGTERM
  - [ ] Human review required (Class A)
- Files allowed: `config/`, `engine/src/apps/trader/`, `engine/src/calendar/` (stub only), `engine/tests/`, `tools/`
- Out of scope: strategies, orders, broker adapters
- Tests required: TC-CF-01 to TC-CF-09 as applicable at Phase 0

### T-0008: Journal inspector
- Status: In review
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
  - `just check`: CTest 13/13, journal schema tests, watchdog link check
- Files allowed: `tools/journal_inspect/`, `tests/fixtures/compat/v1/`
- Out of scope: writer; repair; any change to the journal format
- Tests required: fixture tests for each case; read-only check (file hash unchanged after run)

### T-0009: Python research package skeleton
- Status: Backlog
- Phase: 0 · Risk class: B · Depends on: T-0002
- Requirements: FR-RS-01 (scaffold), TI §4.2
- Goal: `research/` is a uv project with lint, types, tests, and a `tradelab.engine` stub.
- Acceptance criteria:
  - [ ] `research/pyproject.toml`, `uv.lock`, `.python-version` (3.12)
  - [ ] `ruff` and `mypy --strict` configured and passing on the skeleton
  - [ ] `tradelab.engine` module exists as a stub that raises `NotImplementedError` with a message referencing ADR-0005
  - [ ] pytest runs with one smoke test
- Files allowed: `research/`, `justfile`
- Out of scope: data ingestion, backtesting, pybind11 build
- Tests required: smoke test; lint and types clean

### T-0010: Frontend skeleton with smoke test
- Status: Backlog
- Phase: 0 · Risk class: B · Depends on: T-0002
- Requirements: FR-UI-01 (scaffold), TI §4.3
- Goal: Static dashboard shell with module structure, a reducer, and a Playwright smoke test. No live data.
- Acceptance criteria:
  - [ ] `ui/index.html`, `ui/css/`, `ui/js/state/`, `ui/js/views/`, `ui/js/transport/`
  - [ ] Pure reducer with unit tests (Node test runner or Vitest; choice recorded in an ADR if a new dependency is needed)
  - [ ] Dashboard shell shows the panels in USER_OPERATIONS §5.1 with placeholder "no data" state
  - [ ] Playwright smoke test loads the page and checks every panel heading exists
  - [ ] No `innerHTML` with data; lint rule enforces this if a linter is configured
- Files allowed: `ui/`, `justfile`
- Out of scope: control API; live data; kill controls (Phase 5)
- Tests required: reducer unit tests; Playwright smoke

### T-0011: Calendar and instrument master loader
- Status: Backlog
- Phase: 0 · Risk class: B · Depends on: T-0007
- Requirements: FR-MD-04 (session data), TI §8, DESIGN §8.3
- Goal: Load and validate trading calendars and instrument master files. No real operator data.
- Acceptance criteria:
  - [ ] Calendar format supports sessions per product, holidays, early closes, and maintenance windows
  - [ ] Instrument master format: symbol, venue, lot size, tick size, expiry, strike, option type, underlying
  - [ ] Validator rejects overlapping sessions, zero or negative tick or lot sizes, and duplicate symbols
  - [ ] Sample files use synthetic values and are marked `# SAMPLE — NOT FOR TRADING`
  - [ ] Loader exposes lookup by symbol and session query by time, with a fixed-clock test
- Files allowed: `engine/src/calendar/`, `engine/src/instruments/`, `config/calendars/`, `config/instruments/`, `engine/tests/`
- Out of scope: real instrument data; roll logic (Phase 2); session behavior in strategies
- Tests required: validator tests; lookup tests; session query at boundaries

## Phase 1 Backlog (Not Yet Ready)

Tickets for Phase 1 are written at the Phase 0 exit review. Do not begin them in Phase 0.

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
| Q-9 | T-0004 branch stacks `feat/T-0003-cmake-superbuild` until T-0003 is on `main`. Confirm PR base and merge order with operator. | T-0004 review | Open |
| Q-10 | Wire `core_tests` into CTest and add `just test-tsan`. | T-0005 CI | Done — merged in `ffaece3` |
| Q-12 | Partitioned-table unique keys include `event_time` (option A). | T-0006 | Done — operator approved 2026-10-29; ADR-0013 Accepted |
| Q-11 | Add Google Benchmark to `vcpkg.json` (ADR) for nightly SPSC regression vs `engine/bench/core/baseline.txt`. T-0005 used std::chrono bench only. | Phase 3 perf gates | Open |

## Decisions Log

| Date | Decision | Reference |
|---|---|---|
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
| 2026-10-29 | cursor | T-0008 | (see branch) | In review | Journal inspector CLI, CRC/sequence scan, torn-tail AL-14, compat v1 fixtures, read-only tests. |
| 2026-10-29 | cursor | T-0005 | `e0f2d4d`, `3d11e6c` | In review | Core Clock, SpscRing, ObjectPool, LogQueue, pin_thread, allocation hook; 10M SPSC stress; TSan clean. |
| 2026-10-29 | cursor | T-0004 | `890a6c5`, `574d14c` | In review | Journal v1 schema, codegen drift check, C++/Python round-trip tests; branch includes T-0003 build. |
| 2026-10-29 | cursor | T-0003 | `4e544e6`, PROGRESS commit | In review | CMake superbuild, six presets, vcpkg gtest, empty engine targets, watchdog link check. |
| 2026-10-29 | cursor | T-0002 | `756a218` on `main` | Done | Hooks + CI skeleton; pushed to GitHub `origin`. |
| 2026-10-29 | cursor | T-0002 | `09cfc89` (merge T-0001), `e3bf1dc`, PROGRESS commit | Done | Merged T-0001 to `main`. T-0002 hooks + CI skeleton. Pre-commit demo blocked synthetic secret. |
| 2026-10-29 | cursor | T-0001 | `8086b9f`, `a9defd7`, and the commit that adds this row | Done | Repo was empty. `git` 2.54.0. `user.name` and `user.email` were set. No remote configured. `just` is not on PATH and was not installed. `tools/check_repo_skeleton.sh`: `skeleton check passed (287 paths)`. PR not opened: no remote. |
| 2026-10-25 | human | — | — | Kickoff documents created | Starting point for T-0001 |

## Blockers

None recorded.
