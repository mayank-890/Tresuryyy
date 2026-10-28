# PROGRESS.md

Single source of truth for the current state of work. Read this first in every session. Update it at every checkpoint.

## Handoff

- Updated: 2026-10-25 by human (project kickoff)
- Active ticket: T-0001
- Branch: none yet (create `chore/T-0001-repo-skeleton` from `main`)
- Last green commit: none (repository not yet initialized)
- Working tree: n/a
- Tests passing at last commit: n/a
- Next step (exact): Create the repository root with the document set (`PRD.md`, `DESIGN_ARCHITECTURE.md`, `TECHNICAL_IMPLEMENTATION.md`, `PROCESS_PROJECT_MANAGEMENT.md`, `TESTING_QA.md`, `USER_OPERATIONS.md`, `AGENTS.md`, `PROGRESS.md`, `AGENT_PROMPTS.md`), then execute T-0001 acceptance criteria.
- Files touched this ticket: none yet
- Decisions made this session (not yet in ADR): Codex is the primary implementation agent; Cursor is the fallback. Handoff via this file and `AGENTS.md`.
- Open questions: see §Open Questions below
- Notes for the next tool: Phase 0 only. Do not begin Phase 1+ work. Do not add any trading logic in Phase 0.

## Current Phase

**Phase 0: Foundation (weeks 1–2).** Exit criteria: PROCESS_PROJECT_MANAGEMENT.md §6.1, row 0.

## Phase 0 Backlog

Status values: `Backlog` · `Ready` · `In progress` · `In review` · `Done` · `Blocked`

### T-0001: Repository skeleton
- Status: Ready
- Phase: 0 · Risk class: C · Depends on: none
- Requirements: PRD §5 (roles), TI §2 (layout)
- Goal: Repository initialized with the full document set and the directory skeleton from the repository structure. No code.
- Acceptance criteria:
  - [ ] Git repository initialized; `main` branch exists; first commit contains only the documents and `.gitignore`
  - [ ] `.gitignore` excludes: build outputs, `.handoff/`, `.venv/`, `*.env`, `secrets/`, `data/`, `*.journal`, IDE and tool-local settings
  - [ ] `.editorconfig` present (UTF-8, LF, final newline, 4-space C++, 4-space Python, 2-space YAML/TOML/JSON)
  - [ ] Directory skeleton exists with `README.md` placeholders in each top-level folder: `engine/`, `research/`, `services/`, `ui/`, `schemas/`, `proto/`, `db/`, `config/`, `deploy/`, `tests/`, `tools/`, `docs/`, `docker/`, `.github/workflows/`
  - [ ] `docs/adr/` contains ADR-0001 to ADR-0012 as stubs with status and title from DESIGN_ARCHITECTURE.md §18
  - [ ] `docs/tickets/` and `docs/reviews/` exist with `.gitkeep`
  - [ ] `README.md` at root: one-paragraph description, pointer to `AGENTS.md`, pointer to the docs set, status "Phase 0"
  - [ ] `CHANGELOG.md` with an `Unreleased` section
- Files allowed: all new files listed above; no other paths
- Out of scope: any source code, build configuration, CI, database, config profiles
- Tests required: none beyond a script check that every required file exists (`tools/check_repo_skeleton.sh`, added in this ticket)

### T-0002: Hooks, secret scanning, CI skeleton
- Status: Backlog
- Phase: 0 · Risk class: C (CI safety gates: A) · Depends on: T-0001
- Requirements: NFR-SEC-01, TI §10.2, TI §10.3, TESTING_QA §10
- Goal: Pre-commit hooks and a CI workflow that run on every PR and currently build nothing.
- Acceptance criteria:
  - [ ] `.pre-commit-config.yaml` with: gitleaks, large-file check (>1 MB outside `tests/fixtures/`), trailing whitespace, end-of-file fixer
  - [ ] `.gitleaks.toml` with repository allowlist empty
  - [ ] `.github/workflows/ci.yml` runs: secret scan, large-file check, skeleton check (T-0001 script); all required-check names reserved (job names may be placeholders that pass with a `TODO(T-0003)` note)
  - [ ] Demonstrated: a test commit containing a fake secret pattern is blocked locally by pre-commit (record the command and output in the PR)
- Files allowed: `.pre-commit-config.yaml`, `.gitleaks.toml`, `.github/workflows/`, `tools/`
- Out of scope: any build step
- Tests required: pre-commit dry-run on a fixture secret; CI workflow linted (`actionlint` if available)

### T-0003: CMake superbuild, presets, vcpkg manifest, CTest
- Status: Backlog
- Phase: 0 · Risk class: B · Depends on: T-0002
- Requirements: TI §3.1–§3.4
- Goal: Engine builds with each preset; empty targets exist; CTest runs; the watchdog link-restriction check exists (currently passes trivially).
- Acceptance criteria:
  - [ ] Top-level `CMakeLists.txt` and `CMakePresets.json` with presets: `debug-asan`, `debug-tsan`, `debug-ubsan`, `release`, `bench`, `coverage`
  - [ ] Options: `TRADING_ENABLE_GROWW=OFF`, `TRADING_BUILD_BINDINGS=OFF`, `TRADING_BUILD_WATCHDOG=ON`, `TRADING_ENABLE_LIVE_PROFILE=OFF`. Groww option verified OFF in every preset.
  - [ ] `vcpkg.json` with GoogleTest only at this stage; baseline pinned
  - [ ] Empty static libraries from TI §3.3 exist with one smoke test each
  - [ ] `trader` and `watchdog` placeholder executables print version and exit 0
  - [ ] `just build` and `just test` work on each preset
  - [ ] A check script fails if `watchdog` links any `trading_strategy` or `trading_risk` target
- Files allowed: `engine/`, `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `justfile`, `tools/`
- Out of scope: real logic in any module; third-party libraries beyond GoogleTest (each needs an ADR)
- Tests required: CTest smoke tests per library; the watchdog link check runs in CI

### T-0004: Journal schema v1 and codegen
- Status: Backlog
- Phase: 0 · Risk class: A · Depends on: T-0003
- Requirements: FR-JR-01, DESIGN §10, TI §5
- Goal: `schemas/journal/v1/records.yaml` defines the header and the record types in TI §5.2; a generator produces C++ and Python bindings; drift check in CI.
- Acceptance criteria:
  - [ ] Header layout exactly as DESIGN §10.1 (40 bytes, little-endian); static assertion on size and offsets in C++
  - [ ] Record type IDs as in TI §5.2
  - [ ] `just codegen` generates `records.hpp`, `services/.../generated.py`, `research/.../generated.py`
  - [ ] `just codegen` fails if output differs from committed files
  - [ ] Round-trip test: encode and decode each record type in C++ and Python; results identical
  - [ ] `schemas/journal/CHANGELOG.md` created with v1 entry
  - [ ] Human review required (Class A). Do not merge without it.
- Files allowed: `schemas/`, `engine/include/trading/journal/`, `services/` (generated only), `research/` (generated only), `justfile`
- Out of scope: writer or reader logic (T-0008 and Phase 3)
- Tests required: round-trip per record type; header size and offset tests; codegen drift test

### T-0005: Core primitives
- Status: Backlog
- Phase: 0 · Risk class: A · Depends on: T-0004
- Requirements: NFR-PERF-02, TI §4.1, DESIGN §4
- Goal: Clock, SPSC ring, object pool, lock-free log queue, thread-pinning helper. Tested under TSan and ASan.
- Acceptance criteria:
  - [ ] `trading::core::Clock` wrapping `steady_clock` for mono and wall time, with no allocation
  - [ ] `SpscRing<T, N>` with `alignas(64)` indices, explicit memory orders with comments, power-of-two capacity
  - [ ] `ObjectPool<T>` preallocated, no allocation after construction, double-release detection in debug builds
  - [ ] `LogQueue` lock-free producer side, drained by a dedicated thread (stub sink in this ticket)
  - [ ] `pinThread(core)` helper with error result; no-op with a logged warning on platforms without affinity support
  - [ ] Allocation-counting test hook usable by later tests
  - [ ] Tests: single-producer single-consumer stress (1 billion items in nightly, 10 million in PR); wraparound; full and empty boundaries; TSan clean
  - [ ] Human review required (Class A)
- Files allowed: `engine/include/trading/core/`, `engine/src/core/`, `engine/tests/unit/core/`, `engine/bench/core/`
- Out of scope: any use of these primitives in strategy, risk, or broker code
- Tests required: unit; TSan stress; ASan; benchmark baseline recorded

### T-0006: PostgreSQL migrations and partition scripts
- Status: Backlog
- Phase: 0 · Risk class: A · Depends on: T-0002
- Requirements: FR-JR-03, DESIGN §11, TI §11
- Goal: Initial schema, roles, indexes, partition management; applied and rolled back in CI.
- Acceptance criteria:
  - [ ] `docker/compose.yml` with PostgreSQL 16 bound to localhost
  - [ ] dbmate migrations: schema `trading`; tables from DESIGN §11; roles `projector_writer`, `control_api_reader`, `report_reader` with least privilege
  - [ ] Unique constraints on `(source_segment, source_sequence)` in every projected table
  - [ ] Indexes per TI §11.1
  - [ ] Partition scripts create daily and monthly partitions ahead of time; drop only after archive verification flag is set
  - [ ] `projection_checkpoint` table
  - [ ] CI: apply all migrations to a fresh database; run down-migrations; re-apply
  - [ ] Human review required (Class A)
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
- Status: Backlog
- Phase: 0 · Risk class: C · Depends on: T-0004, T-0005
- Requirements: FR-JR-02, TC-JR-02, TC-JR-10
- Goal: Read-only tool that validates journal segments, reports torn tails, and pretty-prints records.
- Acceptance criteria:
  - [ ] Reads v1 segments; validates CRC and sequence continuity
  - [ ] Detects a torn tail and reports it without modifying the file (`--repair` is NOT implemented in this ticket)
  - [ ] Mid-file CRC failure reported as fatal with byte offset
  - [ ] Filtering by record type and sequence range
  - [ ] Fixture set in `tests/fixtures/compat/v1/` (small, synthetic) covering normal, torn-tail, and CRC-corrupt cases
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

## Decisions Log

| Date | Decision | Reference |
|---|---|---|
| 2026-10-25 | Codex is primary; Cursor is fallback; both read AGENTS.md | PROCESS §1.1, §7.6 |
| 2026-10-25 | Phase 0 scope is limited to tickets T-0001 to T-0011 | PROCESS §6.2 |
| 2026-10-25 | Groww adapter remains behind OFF flag; mock only until G-LIVE-1 and G-LIVE-2 | PRD §12; ADR-0004 |

## Session Log

Append one entry per session. Newest first.

| Date (IST) | Tool | Ticket | Commits | Result | Notes |
|---|---|---|---|---|---|
| 2026-10-25 | human | — | — | Kickoff documents created | Starting point for T-0001 |

## Blockers

None recorded.
