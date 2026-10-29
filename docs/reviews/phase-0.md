# Phase 0 review — Foundation

- Date: 2026-10-29
- Reviewer: operator (pending sign-off)
- Git baseline: `main` @ `8726a22` (`Merge T-0011: calendar and instrument loaders`)

## Exit criteria (PROCESS §6.1, row 0)

| Criterion | Status | Evidence |
|---|---|---|
| Clean clone builds and tests pass | Met (agent) | `just check` on **Darwin arm64**, 2026-10-29 (see below) |
| Migrations apply | Met | T-0006; `just migrate` / `just test-db` evidence in PROGRESS T-0006 |
| Secrets scan clean | Met | T-0002 pre-commit gitleaks; CI `secret-scan` job |
| Sanitizers clean | Met (default preset) | CTest **debug-asan** 14/14; TSan verified T-0005/T-0007 in PROGRESS |
| Engine starts in paper mode with no trading | Met | `trader_version` CTest; TC-CF-* in `profile_tests` |
| Journal inspector reads fixtures and detects torn tail | Met | T-0008; `tools/journal_inspect/run_tests.sh` |

## Phase 0 tickets (T-0001–T-0011)

| Ticket | Title (short) | Status on `main` | Merge / tip SHA |
|---|---|---|---|
| T-0001 | Repository skeleton | Done | `09cfc89` (`Merge T-0001: repository skeleton into main`) |
| T-0002 | Hooks, secret scanning, CI skeleton | Done | `756a218` (T-0002 handoff on `main`) |
| T-0003 | CMake superbuild, presets, CTest | Done | Done on `main` (stack through `b98a565` / T-0004 merge) |
| T-0004 | Journal schema v1 and codegen | Done | `b98a565` |
| T-0005 | Core primitives | Done | `ffaece3` |
| T-0006 | PostgreSQL migrations | Done | `080a18d` |
| T-0007 | Profiles, validator, promotion verifier | Done | `a3019bf` |
| T-0008 | Journal inspector | Done | `bcb67f9` |
| T-0009 | Python research package skeleton | Done | `f6bbe81` |
| T-0010 | Frontend skeleton | Done | `8251a18` |
| T-0011 | Calendar and instrument master loader | Done | `8726a22` |

## Verification run (2026-10-29, Darwin arm64)

Command: `just check` on `main` @ `8726a22`.

- Codegen drift: pass
- Journal schema tests: **22/22** (Python + C++ round-trip)
- Research: ruff, mypy strict, pytest **1/1**
- UI unit tests: **4/4** (reducer + innerHTML guard)
- CTest **debug-asan**: **14/14**
- Watchdog link check: pass

## Deviations and notes

- **Q-14:** T-0011 required a minimal `engine/CMakeLists.txt` amendment (`trading_profile` → `trading_core`, `engine/include`). Operator acceptance recorded as open in PROGRESS until signed.
- **ADR-0014** (config deps): still **Proposed** (Q-13); already used on `main` for T-0007 — operator sign-off pending.
- **UI e2e:** Playwright smoke is `just ui-test` (not in default `just check`); run in CI with `just ui-test-ci` when wired.

## Open questions and blockers (carry forward)

| ID | Summary | Blocks Phase 1? |
|---|---|---|
| Q-1–Q-7 | Groww API, registration, data sources, capital, logging, gitignore, etc. | Partial (see PRD) |
| Q-11 | Google Benchmark ADR for SPSC regression | No (Phase 3 perf) |
| Q-13 | Accept ADR-0014 (toml++/json/openssl) | No (already on `main`) |
| Q-14 | Accept T-0011 `engine/CMakeLists.txt` amendment | No |
| B-0101-3 | **pyarrow** for research Parquet I/O | **Yes** — ADR-0015 **Proposed**; operator must **Accept** before adding dependency |

## Decision

**Phase 0 is complete. Phase 1 may begin once B-0101-3 is resolved.**

Operator action required:

1. Review this note and confirm Phase 0 exit.
2. **Accept ADR-0015** (pyarrow for research-side Parquet only) to clear B-0101-3.
3. Then authorize **T-0101** (Delta historical OHLCV ingestion) on a feature branch.
