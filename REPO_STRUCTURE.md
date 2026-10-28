# Repository Structure

## Automated Derivatives & Commodities Trading System

| Field | Value |
|---|---|
| Document ID | RS-001 |
| Version | 0.1 |
| Related | TECHNICAL_IMPLEMENTATION.md §2 (ownership and review by path), PROCESS_PROJECT_MANAGEMENT.md §3 (risk classes), AGENTS.md §9 |

This document is the authoritative directory layout. Phase 0 ticket T-0001 creates every directory listed here. Directories are created with a `README.md` placeholder describing their purpose. No source code is created by T-0001.

---

## 1. Principles

1. **Monorepo.** The C++ engine, Python research, services, UI, schemas, and infrastructure share one repository, so a change touching the journal format or the strategy library lands atomically across all consumers.
2. **One source of truth for shared formats.** Journal record definitions live in `schemas/`; control-plane definitions live in `proto/`. Code for both C++ and Python is generated from them.
3. **Trading code is isolated.** The hot path lives in `engine/`. Nothing in it depends on Python, the UI, or the database.
4. **Secrets never enter the repository.** Configuration files contain secret names, never values.
5. **Large data stays out of git.** Market data archives and Parquet files live under an external data root. The repository holds only small fixtures.

---

## 2. Top-Level Layout

```
trading-system/
├── engine/                  # C++: live execution engine, strategy library, risk gate
├── research/                # Python: backtesting, analytics, risk models, light ML
├── services/                # Python: ingestion, projector, control API
├── ui/                      # HTML/CSS/JS monitoring dashboard (no build step)
├── schemas/                 # Journal record definitions (versioned; single source of truth)
├── proto/                   # gRPC control-plane definitions
├── db/                      # PostgreSQL migrations, partition scripts, seed data
├── config/                  # Non-secret profiles, schemas, calendars, instrument seeds, QA tolerances
├── deploy/                  # systemd units, chrony and sysctl config, CPU isolation
├── tests/                   # Cross-component tests: mock exchange, chaos, e2e, fixtures
├── tools/                   # Operator tooling: journal inspector, reconciliation check, kill drill
├── docs/                    # ADRs, architecture notes, runbooks, compliance, reviews, tickets, incidents
├── docker/                  # Local PostgreSQL compose file, dev container
├── .github/workflows/       # CI definitions
├── .handoff/                # Git-ignored: saved patches from interrupted sessions
├── CMakeLists.txt           # Top-level CMake superbuild
├── CMakePresets.json        # Named build presets
├── vcpkg.json               # C++ dependency manifest
├── justfile                 # Task runner: single entry points for common workflows
├── .clang-format
├── .clang-tidy
├── .editorconfig
├── .pre-commit-config.yaml
├── .gitleaks.toml
├── .gitignore
├── CHANGELOG.md
├── AGENTS.md                # Agent rules (also: PRD, DESIGN_ARCHITECTURE, TECHNICAL_IMPLEMENTATION,
├── PROCESS_PROJECT_MANAGEMENT.md, TESTING_QA.md, USER_OPERATIONS.md, REPO_STRUCTURE.md,
├── PROGRESS.md, AGENT_PROMPTS.md   #  project documents live at the root during Phase 0
└── README.md
```

---

## 3. Engine (C++)

```
engine/
├── CMakeLists.txt
├── cmake/
│   └── toolchains/          # Compiler toolchain files
├── include/trading/         # Public headers only; the only headers other modules may include
│   ├── core/                # Clock, object pools, SPSC ring, thread pinning, log queue
│   ├── marketdata/          # Quote, Trade, Instrument types
│   ├── orders/              # Order, OrderState, ClientOrderId
│   ├── broker/              # IBroker interface, error taxonomy, capabilities
│   ├── risk/                # RiskDecision, ApprovalToken (limit values are NOT here)
│   └── journal/
│       └── generated/       # Generated from schemas/journal (do not edit)
├── src/
│   ├── core/
│   ├── marketdata/          # Normalizer, order book, sequence and gap detection, staleness
│   ├── strategy/            # Strategy base, signal, sizing, parameter snapshot double buffer
│   │   └── strategies/      # Concrete strategies, one subdirectory each, registered at compile time
│   ├── risk/                # Risk gate, limit checks, throttle, cancel-ratio monitor
│   │   └── limits.hpp       # Compile-time limit values (operator-owned; agents do not change values)
│   ├── orders/              # Order state machine, order manager, client ID generator
│   ├── portfolio/           # Positions, PnL, equity curve, reconciler
│   ├── journal/             # Writer (group fsync), reader, replay driver, recovery
│   ├── broker/
│   │   ├── common/          # Token-bucket rate limiter, retry policy, reconnect backoff
│   │   ├── delta/           # Delta REST client, WebSocket feeds, HMAC signing, adapter
│   │   ├── groww/           # Groww adapter; compiled only when TRADING_ENABLE_GROWW=ON
│   │   └── mock/            # Mock exchange adapter for tests and paper mode
│   ├── control/             # Control socket server and client
│   ├── telemetry/           # Snapshot publisher and shared-memory writer
│   ├── calendar/            # Sessions, holidays, maintenance windows, roll dates
│   ├── instruments/         # Instrument master loader and lookup
│   └── apps/
│       ├── trader/          # Live engine executable
│       ├── replayer/        # Replays journal or recorded ticks through the engine
│       └── watchdog/        # Separate executable (minimal dependencies)
├── bindings/
│   └── python/              # pybind11 module: strategy library and backtest core
├── tests/
│   ├── unit/
│   ├── fuzz/                # WebSocket frame, REST response, journal, config parsers
│   ├── integration/         # In-process pipeline tests against the mock adapter
│   └── replay/              # Decision parity tests
├── bench/                   # Google Benchmark suites
└── third_party/             # Vendored headers only where package management is impractical
```

### Engine layout rules

- **`include/` vs `src/`.** Public headers define contracts between modules. Internal headers stay in `src/`. A CI check enforces this.
- **Strategies are compiled in.** Each registers through a static registry. No runtime plugin loading (ADR-0003).
- **Watchdog is a separate target** that links only broker REST, control client, and journal reader. It must not link `trading_strategy` or `trading_risk` (ADR-0010).
- **Groww is behind a build flag** `TRADING_ENABLE_GROWW`, OFF by default (ADR-0004).
- **Risk limit values live only in `engine/src/risk/limits.hpp`.** Runtime configuration may tighten them, never loosen them (ADR-0009).

---

## 4. Research (Python)

```
research/
├── pyproject.toml           # uv project; pins numpy, polars, pyarrow; engine wheel dependency
├── uv.lock
├── .python-version          # 3.12
├── src/tradelab/
│   ├── engine/              # Thin wrapper over the pybind11 module; the only path to the engine
│   ├── data/
│   │   ├── ingest/          # Historical OHLCV loaders
│   │   ├── quality/         # Gap detection, outlier flags, stale-bar checks
│   │   ├── adjust/          # Futures roll adjustment, corporate actions
│   │   └── store/           # Parquet layout and read/write helpers
│   ├── features/            # Feature definitions with reference implementations for parity
│   ├── models/              # Light ML training, inference wrappers, artifact export
│   ├── risk_models/         # Volatility, VaR, stress models, Greeks utilities
│   ├── backtest/
│   │   ├── runner.py
│   │   ├── walkforward.py
│   │   └── fillmodel/       # Fill model calibration
│   ├── reporting/           # Tear sheets, paper and live comparison
│   ├── journal/
│   │   └── generated.py     # Generated from schemas/journal (do not edit)
│   └── cli/                 # tradelab command-line entry points
├── tests/
│   ├── parity/              # Python reference implementations checked against engine output
│   └── unit/
├── notebooks/               # Exploratory only; nbstripout enforced
└── configs/                 # Experiment definitions (YAML); hashes recorded in results
```

### Research rules

- Research reaches the engine only through `tradelab.engine`.
- Notebooks are scratch space. Anything producing a decision-relevant result moves into `src/` with a test.
- Every result records config hash, data snapshot ID, and engine build hash.

---

## 5. Services, UI, Database, Schemas, Protocol, Configuration

```
services/
├── pyproject.toml           # uv project
├── uv.lock
├── src/tradelab_services/
│   ├── journal/
│   │   └── generated.py     # Generated from schemas/journal (do not edit)
│   ├── ingestion/           # Tick recorder for paper and live capture; backfill jobs
│   ├── projector/           # Journal to PostgreSQL, batched COPY, idempotent
│   └── control_api/         # Localhost HTTP server for the UI; translates to control-socket calls
└── tests/

ui/
├── index.html
├── css/
├── js/                      # ES modules, no bundler
│   ├── state/               # Store and pure reducers
│   ├── views/               # PnL, positions, orders, feed health, margin, risk events
│   └── transport/           # Polling and command transport to the control API
├── tests/                   # Reducer unit tests
└── e2e/                     # Playwright tests

db/
├── migrations/              # dbmate, forward-only; never edit a merged migration
├── partitions/              # Scripts creating and dropping partitions per retention policy
└── seed/                    # Reference data only; no trading data

schemas/
├── journal/
│   ├── v1/
│   │   └── records.yaml     # Header and record type definitions (source of truth)
│   └── CHANGELOG.md
└── codegen/                 # Generators for C++ and Python bindings

proto/
└── control/
    └── v1/
        └── control.proto

config/
├── profiles/
│   ├── paper.toml
│   ├── testnet.toml
│   └── live.toml            # Secret names only; reviewed as Class A
├── schema/
│   └── profile.schema.json
├── calendars/
├── instruments/             # Seed files; synthetic samples marked NOT FOR TRADING
├── risk/
│   └── overrides.toml       # Tighten-only runtime overrides
└── qa/
    └── tolerances.toml      # Backtest-to-live tolerances (TESTING_QA §5.3)

deploy/
├── systemd/                 # trader, watchdog, projector, control_api, ingestion units
├── host/                    # chrony.conf, sysctl, isolcpus and IRQ affinity notes
└── secrets/                 # Secret layout documentation; no values
```

---

## 6. Tests, Tools, Documentation, Docker

```
tests/
├── fixtures/
│   ├── compat/
│   │   └── v1/              # Journal and protocol fixtures for compatibility tests
│   └── generators/          # Seeded fixture generators
├── mock_exchange/
│   └── scenarios/           # Scripted scenario files (TOML)
├── chaos/                   # Chaos harness scenarios CH-01 to CH-18
├── e2e/                     # Full-stack runs
└── traceability.csv         # Requirement to test mapping (TESTING_QA §11)

tools/
├── journal_inspect/         # Read-only journal validator and printer
├── reconcile/               # Journal-to-broker comparison
├── kill_drill/              # Weekly kill-switch exercise
└── check_repo_skeleton.sh   # Verifies required paths exist (T-0001)

docs/
├── adr/                     # ADR-0001 to ADR-0012 and later; never deleted
├── architecture/            # Diagrams and notes
├── runbooks/                # RB-01 to RB-15 detail
├── compliance/              # Regulatory and broker-terms records
├── reviews/                 # Phase reviews, pre-live checklists, drill records
│   └── drills/
├── incidents/               # Incident records (YYYY-MM-DD-slug.md)
├── tickets/                 # Ticket specs for tickets needing more than PROGRESS.md
└── qa/
    └── ui-checklist.md

docker/
├── compose.yml              # PostgreSQL 16, localhost-bound
└── dev/                     # Dev container definition (optional)

.github/
└── workflows/
    └── ci.yml
```

Empty directories that Git does not track receive a `.gitkeep` or a `README.md`. T-0001 creates a `README.md` in every directory above that contains no other file, stating its purpose in one or two lines.

---

## 7. Ownership and Risk Class by Path

| Path | Risk class | Review |
|---|---|---|
| `engine/src/risk/`, `engine/src/orders/`, `engine/src/journal/`, `engine/src/broker/`, `engine/src/control/`, `engine/src/apps/watchdog/` | A | Human, required |
| `engine/include/trading/` (contracts) | A where it affects the above | Human, required |
| `db/migrations/`, `schemas/journal/`, `proto/control/` | A | Human, required |
| `config/profiles/live.toml`, `config/risk/`, `deploy/` | A | Human, required |
| `.github/workflows/` (safety gates) | A | Human, required |
| `engine/src/strategy/`, `engine/src/marketdata/`, `engine/src/portfolio/` | B | Human for first PR per module, then standard |
| `research/`, `services/`, `ui/`, non-live `config/` | B | Standard |
| `docs/`, `tools/`, `tests/fixtures/`, formatting | C | Standard |

---

## 8. Dependency Rules

Enforced by CI (TECHNICAL_IMPLEMENTATION.md §10.3). Any violation fails the build.

| From | May depend on | Must not depend on |
|---|---|---|
| `engine/src` | `engine/include`, generated journal C++ | `research/`, `services/`, `ui/`, `db/` |
| `engine/src/apps/watchdog` | `broker/common`, `broker/delta` (REST), `control` (client), `journal` (reader) | `strategy`, `risk` |
| `engine/bindings` | `engine/include` | `apps/*` |
| `research/` | `tradelab.engine` (wheel), generated research journal bindings | `engine/src` directly |
| `services/` | generated services bindings, `db/`, `proto/` | `engine/src` |
| `ui/` | `services/control_api` over HTTP only | Anything else |
| `broker/groww` | `broker/common`, broker interface | Strategy code |

---

## 9. Build Order

Phase 0 creates the skeleton in this order. Each step has its own ticket in PROGRESS.md.

1. T-0001: Repository, documents, every directory above with placeholders, ADR stubs, skeleton check script.
2. T-0002: Pre-commit, secret scanning, CI skeleton.
3. T-0003: CMake superbuild, presets, empty targets, CTest.
4. T-0004: Journal schema and codegen.
5. T-0005: Core primitives.
6. T-0006: Database migrations.
7. T-0007: Profiles, validator, promotion verifier.
8. T-0008: Journal inspector.
9. T-0009: Research package skeleton.
10. T-0010: UI skeleton.
11. T-0011: Calendar and instrument master loader.

No code in `risk/`, `orders/`, or `broker/` is written before T-0004 through T-0007 are merged and reviewed.
