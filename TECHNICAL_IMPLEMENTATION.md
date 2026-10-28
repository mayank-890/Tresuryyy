# Technical and Implementation Document

## Automated Derivatives & Commodities Trading System

| Field | Value |
|---|---|
| Document ID | TI-001 |
| Version | 0.1 |
| Related | DESIGN_ARCHITECTURE.md (what), this document (how), TESTING_QA.md (verification) |

---

## 1. Technology Stack

| Area | Choice | Minimum version | Pinned by |
|---|---|---|---|
| Engine language | C++20 | GCC 13 or Clang 17 | Toolchain file in `engine/cmake/toolchains/` |
| Build | CMake with presets, Ninja | CMake 3.25 | `CMakePresets.json` |
| C++ dependencies | vcpkg manifest mode | Current baseline | `vcpkg.json` and `vcpkg-lock` (baseline pinned) |
| Research and services language | Python | 3.12 | `.python-version` |
| Python environment and lock | uv | Current | `research/uv.lock`, `services/uv.lock` |
| Database | PostgreSQL | 16 | `docker/compose.yml` |
| Migrations | dbmate | Current | `db/migrations/` |
| Task runner | just | Current | `justfile` |
| Frontend | HTML, CSS, ES modules, no bundler | — | — |
| Frontend tests | Playwright | Current | `ui/package.json` (test dependency only) |
| Schema codegen | Custom generator from `schemas/journal/v1` | — | `schemas/codegen/` |
| Control protocol | gRPC over UDS (control plane only) | Current | `proto/control/v1` |

**Version policy.** Toolchains and major libraries are upgraded through a dedicated ticket with its own ADR. Lockfiles are committed. Agents do not upgrade dependencies as a side effect of other work.

## 2. Repository Layout

The layout is defined in the repository structure document. Summary of responsibilities:

| Path | Owner area | Code owner review |
|---|---|---|
| `engine/include/trading/` | Public contracts | Required |
| `engine/src/risk/`, `engine/src/orders/`, `engine/src/journal/` | Safety-critical (Class A) | Required, human |
| `engine/src/broker/` | Class A | Required, human |
| `engine/src/strategy/strategies/` | Strategy code (Class B) | Standard review |
| `research/` | Research (Class B) | Standard review |
| `services/`, `ui/` | Presentation (Class B) | Standard review |
| `db/migrations/` | Class A | Required, human |
| `schemas/journal/` | Class A | Required, human |
| `config/profiles/` | Class A for `live.toml` | Required, human |
| `docs/` | Documentation (Class C) | Standard review |

## 3. Build System

### 3.1 CMake Presets

| Preset | Purpose | Options |
|---|---|---|
| `debug-asan` | Unit and integration tests with AddressSanitizer and UBSan | `-fsanitize=address,undefined` |
| `debug-tsan` | Threading tests | `-fsanitize=thread` |
| `debug-ubsan` | Undefined-behavior checks alone, for quick iteration | `-fsanitize=undefined` |
| `release` | Production binaries | `-O3`, LTO, `-fno-exceptions` for hot-path targets |
| `bench` | Benchmarks | Release plus `-fno-omit-frame-pointer` |
| `coverage` | Coverage reports | `--coverage` |

### 3.2 Options

| Option | Default | Effect |
|---|---|---|
| `TRADING_ENABLE_GROWW` | `OFF` | Compiles the Groww adapter. Must remain OFF until PRD §12 gates are cleared. |
| `TRADING_BUILD_BINDINGS` | `OFF` | Builds the pybind11 module for research |
| `TRADING_BUILD_WATCHDOG` | `ON` | Builds the watchdog executable |
| `TRADING_ENABLE_LIVE_PROFILE` | `OFF` | Compiles live-profile startup path. Enabled only in release builds for live. |

### 3.3 Targets (Engine)

| Target | Type | Links |
|---|---|---|
| `trading_core` | Static library | — |
| `trading_marketdata` | Static library | `trading_core` |
| `trading_strategy` | Static library | `trading_core`, `trading_marketdata` |
| `trading_risk` | Static library | `trading_core`, `trading_strategy` (intent types only) |
| `trading_orders` | Static library | `trading_core` |
| `trading_journal` | Static library | `trading_core` |
| `trading_broker_common` | Static library | `trading_core` |
| `trading_broker_delta` | Static library | `trading_broker_common` |
| `trading_broker_groww` | Static library (conditional) | `trading_broker_common` |
| `trading_broker_mock` | Static library | `trading_broker_common` |
| `trading_control` | Static library | `trading_core`, `trading_journal` |
| `trader` | Executable | All engine libraries except the Groww adapter unless enabled |
| `watchdog` | Executable | `trading_broker_common`, `trading_broker_delta` (REST only), `trading_control` (client), `trading_journal` (reader). Does not link strategy or risk. |
| `trading_bindings` | pybind11 module | `trading_strategy`, `trading_risk`, `trading_core` |

A CI check (§10.3) fails the build if `watchdog` links `trading_strategy` or `trading_risk`.

### 3.4 Test Targets

Each library has a `*_tests` executable using GoogleTest. Fuzz targets use libFuzzer. Benchmarks use Google Benchmark. Test targets are registered with CTest.

## 4. Coding Standards

### 4.1 C++ (Engine)

**Language and style**
- C++20. No compiler extensions.
- `clang-format` and `clang-tidy` are mandatory. Configuration in `.clang-format` and `.clang-tidy`.
- Names: `PascalCase` for types, `camelCase` for functions, `snake_case_` with trailing underscore for private members, `kConstant` for constants, `UPPER_SNAKE` for macros (avoid macros).
- Headers are self-contained. Each header compiles alone (CI check).
- Include order: own header, C system, C++ standard, third-party, project.

**Memory and ownership**
- No raw `new` or `delete`. Use object pools for hot-path objects and `std::unique_ptr` elsewhere.
- No `std::shared_ptr` in hot-path modules.
- Hot-path functions do not allocate. Tests verify with an allocation-counting hook (TESTING_QA.md §3.5).
- Ring buffers and pools are cache-line aligned (`alignas(64)`) for shared indices.

**Concurrency**
- No mutexes in hot-path modules. SPSC rings only, unless an ADR permits otherwise.
- Every `std::atomic` operation states its memory order in a comment at the call site.
- Threads are named and pinned through the `core/thread` helper, never by direct API calls in feature code.
- No `volatile` for synchronization.

**Errors**
- The hot path does not throw. Engine hot-path targets compile with `-fno-exceptions`; results use `trading::Result<T, E>`.
- Errors are typed enums with explicit categories (§6). No string-only errors on the hot path.
- `[[nodiscard]]` on every function returning a `Result` or a decision.

**Numeric**
- Prices are integers in ticks. Quantities are integers in lots or contracts. Floating point is used only in analytics (Greeks, PnL display), never in order sizing or limit checks.
- Overflow-checked arithmetic (`__builtin_*_overflow` or a wrapper) for notional and exposure calculations.

**Logging**
- Structured records through the lock-free queue. Each record carries a level, component, sequence context, and key-value fields.
- No logging of secrets, full API payloads containing auth headers, or account identifiers beyond a masked form.

### 4.2 Python (Research, Services)

- Python 3.12. Type hints required on all public functions. `mypy --strict` on `tradelab.engine` and `services/`.
- `ruff` for lint and format.
- `dataclasses` or `pydantic` models for structured data. No `pickle` for anything persisted or transmitted.
- Journal and schema access only through generated bindings in `schemas/codegen`.
- Async I/O in `control_api` only. The projector is synchronous with batching.
- No global mutable state. Configuration is passed explicitly.

### 4.3 Frontend

- Vanilla ES modules. No framework in v1. Revisit only through an ADR.
- State is held in a single store with pure reducer functions. Rendering functions read state and do not mutate it.
- All text rendered from data uses `textContent`, never `innerHTML` with untrusted data.
- The UI polls `GET /api/status` every 2 seconds and sends commands through `POST` endpoints with CSRF tokens.
- Accessibility: keyboard reachable controls, labelled inputs, sufficient contrast, status conveyed with text as well as color.

### 4.4 SQL

- Lowercase keywords, snake_case identifiers, plural table names.
- Every table has a primary key. Projected tables include `source_segment` and `source_sequence`.
- Migrations are forward-only. Each has an up and a down section; down is tested in CI on non-production databases.

## 5. Schemas

### 5.1 Source of Truth

Journal record definitions live in `schemas/journal/v1/records.yaml`. The codegen step generates:
- `engine/include/trading/journal/generated/records.hpp`
- `services/src/tradelab_services/journal/generated.py`
- `research/src/tradelab/journal/generated.py`

Generated files are committed and checked for drift in CI. Hand edits to generated files fail CI.

### 5.2 Record Types (v1, initial set)

| Type ID | Name | Payload summary |
|---|---|---|
| 0x0001 | `SessionStart` | Profile name, config hash, engine build hash, client ID prefix |
| 0x0002 | `SessionEnd` | Reason, final state |
| 0x0010 | `MarketQuote` | Instrument ID, bid/ask ticks, sizes, exchange time |
| 0x0011 | `MarketTrade` | Instrument ID, price ticks, size, side, exchange time |
| 0x0012 | `FeedState` | Instrument ID, state, reason |
| 0x0020 | `OrderIntent` | Client ID, strategy ID, instrument, side, qty, price, TIF, param version |
| 0x0021 | `ApprovalIssued` | Client ID, limit-set version, expiry |
| 0x0022 | `RiskRejected` | Client ID, limit ID, observed, threshold |
| 0x0023 | `OrderSent` | Client ID, endpoint category |
| 0x0024 | `OrderEvent` | Client ID, from state, to state, broker ID, reason |
| 0x0025 | `FillEvent` | Client ID, fill ID, price ticks, qty, fee, exchange time |
| 0x0030 | `PositionSnapshot` | Per-instrument net qty, avg price, realized PnL |
| 0x0031 | `ReconciliationResult` | Scope, outcome, mismatch details |
| 0x0040 | `ControlCommand` | Command, scope, operator identity, outcome |
| 0x0041 | `LimitTightened` | Limit ID, old value, new value, operator |
| 0x0042 | `StateTransition` | Engine or feed state from/to, reason |
| 0x0050 | `ConfigRecorded` | Config hash, content reference |
| 0x0051 | `PromotionVerified` | Promotion file hash, config hash, outcome |

### 5.3 Compatibility Rules

- Adding a new record type: allowed in the same format version. Older readers skip it.
- Adding a field to an existing type: requires a new record version (for example, `OrderIntent` v2).
- Removing or renaming fields or changing their meaning: forbidden in an existing version.
- Each change is recorded in `schemas/journal/CHANGELOG.md` with a reader-compatibility note.

### 5.4 Control Protocol

Defined in `proto/control/v1/control.proto`. Message and service names follow the command table in DESIGN_ARCHITECTURE.md §12.2. Breaking changes require a `v2` package.

## 6. Error Taxonomy

| Layer | Error type | Examples |
|---|---|---|
| Market data | `FeedError` | `MalformedFrame`, `SequenceGap`, `StaleTimeout`, `SnapshotFailed` |
| Broker | `BrokerError` | `Retryable`, `Rejected`, `Ambiguous`, `Fatal` (DA §9.2) |
| Risk | `RiskDecision` | `Approved`, `Rejected(limitId, observed, threshold)` |
| Orders | `OrderError` | `InvalidTransition`, `UnknownClientId`, `DuplicateIntent` |
| Journal | `JournalError` | `CrcMismatch`, `SequenceGap`, `TornTail`, `DiskFull`, `FsyncFailed` |
| Config | `ConfigError` | `ProfileInvalid`, `PromotionMissing`, `PromotionInvalid`, `HashMismatch` |
| Control | `ControlError` | `Unauthorized`, `InvalidScope`, `NotPermitted` |

A `JournalError` of type `FsyncFailed` or `DiskFull` moves the engine to HALTED immediately. A `CrcMismatch` in the middle of a segment moves it to HALTED and requires operator review.

## 7. Dependencies

### 7.1 Allowlist (requires ADR to add)

| Purpose | Candidate | Status |
|---|---|---|
| Testing (C++) | GoogleTest | Accepted |
| Benchmarks (C++) | Google Benchmark | Accepted |
| Fuzzing | libFuzzer (via Clang) | Accepted |
| Formatting | `fmt` | Accepted |
| TLS | OpenSSL | Accepted |
| HTTP client (REST) | libcurl | Proposed |
| WebSocket client | Boost.Beast | Proposed |
| Logging | Custom lock-free queue with spdlog sink, or pure custom | **Open (Q-5 in PRD §15)**, ADR-0011 |
| Serialization (control) | gRPC and protobuf | Accepted (control plane only) |
| Python bindings | pybind11 | Accepted |
| JSON (config and REST) | simdjson (parse), nlohmann::json (config only) | Proposed |
| Python data | numpy, polars, pyarrow | Accepted |
| Python tests | pytest, hypothesis | Accepted |
| Python lint and types | ruff, mypy | Accepted |

### 7.2 Rules

- New dependency: open an ADR, justify, check license, check vulnerability history, and get operator approval before adding it.
- No dependency may appear on the order path if it allocates or takes locks unless it is benchmarked and documented.
- Pin all dependencies. No floating versions.

## 8. Configuration

### 8.1 Profile File (Example, Non-Secret)

`config/profiles/paper.toml`:

```toml
[profile]
name = "paper"
mode = "paper"                       # paper | testnet | live
engine_build_required = ">=0.1.0"

[execution]
adapter = "mock"                     # mock | delta_testnet | delta_live | groww
client_id_prefix_source = "counter"

[market_data]
instruments_file = "config/instruments/delta_crypto.toml"
calendar = "config/calendars/crypto_24x7.toml"
stale_threshold_ms = 3000            # TODO(operator): confirm per instrument class

[journal]
root = "/var/lib/trading/journal"    # Must be on a separate disk in production
segment_max_mb = 256
group_commit_ms = 2

[risk]
limits_source = "compiled"           # Runtime may only tighten, see ADR-0009
tighten_overrides_file = "config/risk/overrides.toml"

[database]
url_secret = "trading/pg/projector"  # Name only; value comes from secret store

[control]
socket = "/run/trading/control.sock"
```

`live.toml` follows the same schema, sets `mode = "live"`, and additionally requires the `promotion` block. Its values are reviewed as Class A changes.

### 8.2 Validation

- Every profile is validated against `config/schema/profile.schema.json` at startup.
- Unknown keys cause startup failure.
- The engine computes the profile content hash after parsing and writes it to the journal.

### 8.3 Secrets

- Stored in the OS keyring (development) or a secret store on the host (production).
- Referenced by name in profiles. Never by value.
- Loaded into locked memory (`mlock`) and zeroed on shutdown.
- Rotation procedure documented in USER_OPERATIONS.md §9.

## 9. Change Control for Limits and Profiles

| Change | Process |
|---|---|
| Tighten a limit at runtime | `TightenLimit` command through UI or CLI; logged; no restart |
| Loosen a limit | Code change to `limits.hpp`; ADR; two reviews (agent-authored changes require human review); tests updated; restart in paper mode before live |
| Change a live profile value | Class A pull request; ADR if it changes risk posture; promotion file regenerated |
| Change a paper or testnet profile | Standard review |

## 10. Build, Tooling, and Local Development

### 10.1 Justfile Commands

| Command | Action |
|---|---|
| `just setup` | Install toolchains, vcpkg bootstrap, uv sync, pre-commit install, start local Postgres |
| `just build [preset]` | Configure and build the named preset (default `debug-asan`) |
| `just test [preset]` | Run CTest and pytest for changed and all components |
| `just test-tsan` | Run the TSan preset tests |
| `just lint` | clang-format check, clang-tidy, ruff, mypy, frontend lint |
| `just check` | `lint` + `build` + `test` for the default preset. Required before every commit. |
| `just fuzz-smoke` | 60-second run of each fuzz target |
| `just db-up` / `just db-down` | Start or stop local PostgreSQL |
| `just migrate` / `just migrate-down` | Apply or roll back migrations on the local database |
| `just codegen` | Regenerate schema bindings; fails if output differs from committed files |
| `just replay-parity` | Run replay parity against stored reference decisions |
| `just ui-test` | Playwright end-to-end tests |
| `just secrets-scan` | gitleaks over the working tree and staged changes |

### 10.2 Pre-commit Hooks

clang-format, clang-tidy (changed files), ruff, gitleaks, nbstripout, large-file check (> 1 MB blocked outside `tests/fixtures/`), and the codegen drift check.

### 10.3 CI Checks

| Check | Trigger | Blocking |
|---|---|---|
| Build and unit tests (debug-asan, debug-ubsan) | PR | Yes |
| TSan tests on threaded modules | PR | Yes |
| Header self-containment | PR | Yes |
| Include-graph dependency rules | PR | Yes |
| Watchdog link restriction | PR | Yes |
| Fuzz smoke (60 s per target) | PR | Yes |
| Python lint, types, tests | PR | Yes |
| Frontend lint and Playwright | PR | Yes |
| Migration apply and down-check | PR | Yes |
| Codegen drift | PR | Yes |
| Journal compatibility (old fixtures readable) | PR | Yes |
| Replay parity | PR (touching strategy, risk, orders, journal) | Yes |
| Secret scan | PR | Yes |
| Dependency vulnerability scan | PR and nightly | Yes for high severity |
| Full fuzz, chaos suite, benchmark regression | Nightly | Alerts; blocks release |

## 11. Database Implementation

### 11.1 Migrations (Phase 0, T-0006)

Initial migration creates:
- Schema `trading`
- Roles: `projector_writer`, `control_api_reader`, `report_reader`. Least privilege; no superuser for applications.
- Tables from DESIGN_ARCHITECTURE.md §11, with partitioning
- Indexes:
  - BRIN on time columns for tick and event tables
  - B-tree on `(client_order_id)` for `orders`
  - B-tree on `(strategy_id, instrument_id, time)` for `fills`
- Unique constraints on `(source_segment, source_sequence)` for every projected table

### 11.2 Partition Management

- Daily partitions for high-volume tables, created 3 days ahead by a scheduled job.
- Monthly partitions for lower-volume tables.
- Retention: drop partitions only after archive verification (Parquet export or verified backup). Never `DELETE` bulk rows from projected tables.

### 11.3 Projector Batching

- Batch size: 5,000 rows or 500 ms, whichever first.
- Each batch is one transaction. `synchronous_commit = off` allowed only for `market_ticks` and `candles_1m`.
- Batch commit writes the last projected journal sequence to `projection_checkpoint`. Restart resumes from that checkpoint.

## 12. Broker Implementation Guidance

### 12.1 Delta (Phase 4 deliverable)

- **REST authentication:** HMAC-SHA256 over method, timestamp, path, query string, and body (exact canonicalization per the exchange docs, verified in a unit test with a documented sample).
- **Timestamps:** request timestamps taken from the synchronized clock. Clock-health check runs before each session.
- **Client order ID:** set on every order. Used for query and reconciliation.
- **WebSockets:** separate public and private connections. Heartbeat per the exchange's specification. Reconnect with exponential backoff, base 1 s, cap 60 s, jitter ±20%.
- **Reconciliation:** on private-feed reconnect, fetch open orders, positions, and recent fills via REST before resuming.
- **Dead-man switch:** enabled on live where the API provides it. Refresh interval shorter than the engine's cancel-all threshold.
- **Testnet:** all Phase 4 tests run against testnet. Test credentials are stored under the `testnet` secret names.

### 12.2 Groww (Gated)

- Adapter code lives in `engine/src/broker/groww/`, compiled only when `TRADING_ENABLE_GROWW=ON`.
- Phase 4 work proceeds against the mock adapter only. No network calls to Groww in CI or development.
- Any real-endpoint integration requires PRD §12 gates G-LIVE-1 and G-LIVE-2 to be recorded first.
- Authentication (TOTP): seed in secret store; TOTP generated in memory; never logged.
- Session lifecycle: scheduled re-authentication before market open; failure → cancel-only.
- Rate limiting: token bucket at 50% of observed limits, initial values set by configuration.
- If the official API becomes available, a new adapter is written against it. The unofficial adapter is then removed by ADR, not left enabled.

### 12.3 Mock Exchange

- Scripted scenarios in `tests/mock_exchange/scenarios/*.toml`: ack latency, fill schedule, reject rules, timeout injection, 429 injection, disconnect points.
- Deterministic under a seed. Replay of a scenario produces the same sequence of events.

## 13. Trading Logic Implementation Notes

- Strategies live in `engine/src/strategy/strategies/<name>/`, with `strategy.hpp`, `strategy.cpp`, `params.toml` (default parameter version), and `tests/`.
- A strategy registers via a static registrar macro. Duplicate names fail the build.
- Strategy `decide()` receives an immutable `MarketSnapshot` and returns zero or more `OrderIntent` values. It has no other side effects.
- Sizing functions are pure and tested separately.
- Risk limits are never duplicated in strategies. Strategy-level limits are advisory and must be stricter than the gate, never looser.

## 14. Backtesting Implementation Notes

- Event-driven. Events are ordered by `(exchange time, receive time, sequence)`.
- Simulated broker uses the same `IBroker` interface, implemented as the fill-model adapter.
- Fill model parameters are stored in versioned files. Calibration writes a new version; the old version stays.
- Results are written to `research/results/<config-hash>/` with a manifest: config hash, data snapshot ID, engine build hash, date range, parameter versions.

## 15. Observability Implementation

- **Metrics:** counters and histograms exported from `trader` through telemetry to shared memory. `control_api` reads and serves them. Names follow `trading_<component>_<metric>_<unit>`.
- **Latency:** stage timestamps on every intent (received, strategy-out, risk-out, journal-durable, sent, acked). Histograms per stage.
- **Logs:** JSON lines. Required fields: `ts_mono_ns`, `ts_wall`, `level`, `component`, `seq` (when applicable), `msg`, and fields.
- **Correlation:** every log and metric that concerns an order includes its client ID; every journal record has its sequence number.

## 16. Error Handling and Retries (Summary)

| Operation | Retry policy |
|---|---|
| REST order placement | No automatic retry. Ambiguous outcomes go to §5.4. |
| REST query | 3 attempts; delays 1 s and 3 s; jitter |
| REST cancel | Up to 5 attempts with backoff; cancels get rate-limit priority |
| WebSocket reconnect | Exponential backoff, base 1 s, cap 60 s, jitter |
| Journal write | No retry. Failure halts. |
| Projector write | Retry with backoff; buffer in journal |
| Control command | Synchronous; no automatic retry. Client may resend after checking status. |

## 17. Implementation Conventions for Agents

- Work only within the files listed in the ticket. Ticket format is in PROCESS_PROJECT_MANAGEMENT.md §5.2.
- Generated files are never edited by hand. Change the source and run `just codegen`.
- Every new public function has a test. Every bug fix has a regression test that fails before the fix.
- Commit messages begin with the ticket ID: `T-0005: add SPSC ring with release/acquire ordering`.
- Do not add `TODO` without the form `TODO(operator)` or `TODO(ticket-ID)`.
- Do not disable a test with `DISABLED_`, `skip`, or `xfail` without a linked ticket and an approved exception.
