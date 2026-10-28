# Product Requirements Document (PRD)

## Automated Derivatives & Commodities Trading System ("the system")

| Field | Value |
|---|---|
| Document ID | PRD-001 |
| Version | 0.1 (development kickoff) |
| Status | Approved for Phases 0–2. Live-trading requirements are gated (see §12). |
| Owner | Operator (human) |
| Implementers | AI coding agents (Codex primary, Cursor fallback) under human review |
| Companion documents | DESIGN_ARCHITECTURE.md, TECHNICAL_IMPLEMENTATION.md, PROCESS_PROJECT_MANAGEMENT.md, TESTING_QA.md, USER_OPERATIONS.md |

---

## 1. Purpose

Build an automated trading system that trades Indian listed derivatives (futures and options) and commodity contracts, plus crypto derivatives on Delta Exchange. The system must execute a defined set of strategies, enforce hard risk limits that strategy code cannot override, and keep a complete, replayable record of every decision and order.

The system exists to trade with discipline and verifiable correctness. Speed matters only to the extent it does not compromise correctness.

## 2. Problem Statement

Manual or ad hoc automated trading fails in predictable ways: orders are duplicated after network timeouts, positions drift from broker reality, backtests use different logic from live trading, risk limits are checked inconsistently, and there is no record of why a trade happened. This system addresses each of those failure modes directly.

## 3. Goals

| ID | Goal | Measure of success |
|---|---|---|
| G-1 | Correct order handling | Zero duplicate or lost orders in failure-injection tests; zero unexplained position mismatches in paper and live sessions |
| G-2 | Hard, bypass-proof risk control | Every order passes the risk gate; every limit has boundary tests; all three kill paths verified |
| G-3 | Backtest/live parity | Same strategy code produces identical decisions in backtest, replay, and live |
| G-4 | Full auditability | Any order can be traced to its strategy version, parameter version, config hash, and risk decision |
| G-5 | Safe failure | On any unexpected condition, the system stops opening positions and reconciles before resuming |
| G-6 | Maintainable by a small team (one operator plus AI agents) | Every component has documented contracts, tests, and runbooks |

## 4. Non-Goals (v1)

- **No co-location or microsecond-competitive HFT.** Broker round trips go over the public internet. The system optimizes for correctness, with internal latency kept low and measured.
- **No market making in v1.** Strategies are directional, spread, or hedging strategies.
- **No live Groww order placement until §12 gates are cleared.** Groww market data and order paths are built behind a compile-time flag that is OFF by default.
- **No multi-user, multi-tenant, or remote-access features.** The UI is local.
- **No runtime plugin loading.** Strategies are compiled into the engine.
- **No mobile application.**
- **No automatic strategy discovery or self-modifying parameters.** Parameters change only through versioned, reviewed configuration.
- **No support for brokers beyond Groww and Delta** in v1.

## 5. Users and Roles

| Role | Description | Primary needs |
|---|---|---|
| Operator | The human owner. Starts and stops the system, reviews risk, approves promotions to live. | Clear dashboard, reliable kill switch, daily report, clear alerts |
| Researcher | The human, working in Python. Builds strategies, runs backtests, calibrates fill models. | Reproducible experiments, backtest/live parity, fast data access |
| Implementation agent | Codex or Cursor. Implements tickets defined in PROGRESS.md. | Unambiguous ticket specs, clear red lines, resumable state |
| Auditor (future) | Reviews trades and compliance records. | Complete journal, config history, ADRs |

## 6. Scope by Asset Class and Venue

| Venue | Asset classes | Role in v1 | Integration | Enabled by default |
|---|---|---|---|---|
| Delta Exchange | Crypto futures, perpetuals, options | Primary live-capable venue, testnet first | Official REST and WebSocket | Yes (testnet first) |
| Groww | Indian equity derivatives, commodity derivatives | Secondary venue | Unofficial or native APIs (to be validated) | No (build flag OFF) |
| Mock exchange | Simulated | Testing and paper trading | Internal | Yes (tests and paper mode) |

Instrument coverage per venue is defined in `config/instruments/` and maintained by the operator. Agents must not hard-code instrument specifications.

## 7. Functional Requirements

Priority: **MUST** = required for the phase in which it is listed. **SHOULD** = required before live scaling. **COULD** = optional.

### 7.1 Market Data

| ID | Requirement | Priority | Phase |
|---|---|---|---|
| FR-MD-01 | Receive real-time quotes and trades for configured instruments from the Delta public WebSocket channels. | MUST | 3 |
| FR-MD-02 | Normalize broker messages into internal `Quote` and `Trade` types. Malformed frames are rejected and counted; they never crash the market data thread. | MUST | 3 |
| FR-MD-03 | Detect sequence gaps and request a snapshot resync. An instrument is not HEALTHY until resync completes. | MUST | 3 |
| FR-MD-04 | Mark an instrument STALE when no update arrives within its threshold during a trading session. Block new orders on STALE instruments. | MUST | 3 |
| FR-MD-05 | Record every received market message to the journal with monotonic and wall-clock timestamps. | MUST | 1 (capture), 3 (live) |
| FR-MD-06 | Receive Groww market data through the Groww adapter. | SHOULD | 4+ (gated, §12) |

### 7.2 Orders

| ID | Requirement | Priority | Phase |
|---|---|---|---|
| FR-OR-01 | Every order has a deterministic, unique client order ID. The order intent is journaled and fsynced before the order is sent. | MUST | 3 |
| FR-OR-02 | Orders follow the state machine defined in DESIGN_ARCHITECTURE.md §5. Invalid transitions are rejected, logged, and raised as defects. | MUST | 3 |
| FR-OR-03 | Every order must carry an approval token from the risk gate. No code path sends an order without one. | MUST | 3 |
| FR-OR-04 | An order whose outcome is unknown after a timeout enters UNKNOWN. It is resolved by querying the broker by client order ID. A new order with a new ID is never sent to resolve an unknown state. | MUST | 3 |
| FR-OR-05 | Support LIMIT and IOC orders. Market orders are allowed only where the broker reports the capability and the price collar (RL-07) applies. | MUST | 3 |
| FR-OR-06 | Cancel orders by scope: single order, instrument, underlying, strategy, or global. | MUST | 3 |
| FR-OR-07 | Support reduce-only mode, in which only orders that reduce absolute net position are permitted. | MUST | 3 |
| FR-OR-08 | Futures rolls execute as a two-leg operation with a combined risk check. | SHOULD | 4 |

### 7.3 Strategy

| ID | Requirement | Priority | Phase |
|---|---|---|---|
| FR-ST-01 | Strategies implement a common interface and are registered at compile time. | MUST | 2 |
| FR-ST-02 | Parameters are immutable, versioned snapshots. They are swapped only at decision-cycle boundaries. | MUST | 3 |
| FR-ST-03 | Strategy code has no access to broker types, sockets, the journal writer, or the database. It emits intents only. | MUST | 3 |
| FR-ST-04 | Each strategy can be halted individually, and all strategies can be halted globally. | MUST | 3 |
| FR-ST-05 | Each decision records the input snapshot reference and the parameter version. | SHOULD | 3 |

### 7.4 Risk Management

| ID | Requirement | Priority | Phase |
|---|---|---|---|
| FR-RK-01 | Enforce risk limits RL-01 through RL-16 (DESIGN_ARCHITECTURE.md §7). | MUST | 3 |
| FR-RK-02 | Limit values are compile-time constants defined in `engine/src/risk/limits.hpp`. Runtime configuration may only tighten them. | MUST | 3 |
| FR-RK-03 | Provide three independent kill paths: software, watchdog, and exchange-side dead-man switch. | MUST | 5 |
| FR-RK-04 | When the daily loss limit (RL-10) is breached, the affected strategy halts until the next session. | MUST | 3 |
| FR-RK-05 | When the drawdown limit (RL-11) is breached, the global kill switch activates. | MUST | 3 |
| FR-RK-06 | Post-trade margin must stay above the buffer (RL-13). Stale margin data blocks new entries. | MUST | 3 |
| FR-RK-07 | Self-trade prevention rejects orders that would cross the system's own resting orders. | MUST | 3 |

### 7.5 Portfolio and Reconciliation

| ID | Requirement | Priority | Phase |
|---|---|---|---|
| FR-PF-01 | Track positions and realized/unrealized PnL per strategy and per instrument. | MUST | 3 |
| FR-PF-02 | Reconcile positions, open orders, and fills against the broker after every connect, every reconnect, and every 60 seconds during a session. | MUST | 3 |
| FR-PF-03 | A reconciliation mismatch halts trading for the affected scope until the operator clears it. | MUST | 3 |
| FR-PF-04 | Write periodic position snapshots to the journal to speed up recovery. | SHOULD | 3 |

### 7.6 Broker Integration

| ID | Requirement | Priority | Phase |
|---|---|---|---|
| FR-BR-01 | All broker access goes through the `IBroker` interface. Strategy code is independent of any broker. | MUST | 4 |
| FR-BR-02 | Delta REST: place, cancel, and query orders with HMAC-SHA256 signing. | MUST | 4 |
| FR-BR-03 | Delta private WebSocket: order updates, fills, and position changes, with heartbeat and reconnection. | MUST | 4 |
| FR-BR-04 | Rate limiting per endpoint category. Cancels and reduce-only orders have priority over new entries. | MUST | 4 |
| FR-BR-05 | Groww adapter is compiled only when `TRADING_ENABLE_GROWW=ON`. It is OFF by default and may not be enabled until §12 gates are cleared. | MUST | 4 (build), gated (enable) |
| FR-BR-06 | Manage session and authentication lifecycles. Groww sessions are re-authenticated before market open. | MUST | 4 |
| FR-BR-07 | Provide a mock exchange adapter for tests and paper mode, with configurable acks, partial fills, rejects, timeouts, 429 responses, and disconnects. | MUST | 4 |

### 7.7 Persistence

| ID | Requirement | Priority | Phase |
|---|---|---|---|
| FR-JR-01 | The append-only journal is the source of truth. Every order intent, order event, fill, risk decision, control command, and configuration change is written to it. | MUST | 1 |
| FR-JR-02 | The journal survives crashes. On recovery, a torn tail is detected, truncated, and reported as an alert (AL-14). | MUST | 1 |
| FR-JR-03 | A projector writes journal data to PostgreSQL idempotently. A PostgreSQL outage does not stop trading. | MUST | 1 |
| FR-JR-04 | Retention policies are enforced. Data is archived before deletion. | SHOULD | 6 |

### 7.8 Research and Backtesting

| ID | Requirement | Priority | Phase |
|---|---|---|---|
| FR-RS-01 | The backtester uses the same strategy library as the live engine, through the pybind11 binding. | MUST | 2 |
| FR-RS-02 | Event-driven replay of recorded data, with latency injection and a calibrated fill model (spread crossing, size-dependent slippage, fees, queue position for limit orders). | MUST | 2 |
| FR-RS-03 | Walk-forward validation is the default evaluation method. | MUST | 2 |
| FR-RS-04 | Every result records config hash, data snapshot ID, and engine build hash. | MUST | 2 |
| FR-RS-05 | Fill-model calibration uses paper and live fill data. | SHOULD | 6–7 |

### 7.9 User Interface and Control

| ID | Requirement | Priority | Phase |
|---|---|---|---|
| FR-UI-01 | Dashboard shows system state, PnL, positions, order states with journal sequence numbers, feed health, margin utilization, and risk events. Refresh interval is 2 seconds or less. | MUST | 8 (basic in 0) |
| FR-UI-02 | Halt, resume, and kill controls go through the control API with a confirmation step. Every activation is written to the audit log. | MUST | 5 |
| FR-UI-03 | The UI cannot submit orders. | MUST | 8 |
| FR-UI-04 | The UI can tighten limits only. Loosening requires the change process in TECHNICAL_IMPLEMENTATION.md §9. | MUST | 8 |

### 7.10 Operations

| ID | Requirement | Priority | Phase |
|---|---|---|---|
| FR-OP-01 | Starting the engine requires a named profile. Live mode requires a signed promotion file that references the config hash and a paper-trading report. | MUST | 0 (paper), 7 (live) |
| FR-OP-02 | The engine does not trade until startup reconciliation completes cleanly. | MUST | 3 |
| FR-OP-03 | Alerts defined in USER_OPERATIONS.md §8 are emitted and delivered. | MUST | 5 |
| FR-OP-04 | A daily post-trade report is generated. | SHOULD | 6 |
| FR-OP-05 | Processes run under a supervisor with restart limits. | MUST | 5 |

## 8. Non-Functional Requirements

| ID | Requirement | Target | Validation |
|---|---|---|---|
| NFR-PERF-01 | Internal decision latency: market-data frame received → risk decision emitted | p99 ≤ 250 µs on the pinned host, measured with the monotonic clock. Excludes network. This is a target, validated in Phase 3; if infeasible, revise through an ADR. | Benchmark suite, Phase 3 |
| NFR-PERF-02 | No heap allocation on the hot path after warm-up | Zero allocations, verified by an allocation-counting test hook | Unit test, CI |
| NFR-PERF-03 | Journal append latency, excluding the fsync group-commit wait | p99 ≤ 50 µs | Benchmark |
| NFR-REL-01 | No order is sent without a durable intent record | Hard invariant | Integration and chaos tests |
| NFR-REL-02 | Crash-to-ready recovery time | ≤ 60 s for a typical session journal (target) | Chaos test CH-05 |
| NFR-REL-03 | Only one engine instance may run per profile | Enforced by lock lease | Integration test |
| NFR-SAFE-01 | The system fails closed. Any unknown state stops new order entry. | Hard invariant | Chaos and property tests |
| NFR-SEC-01 | Secrets (API keys, TOTP seeds, tokens) are stored only in the OS keyring or secret store. They never appear in the repository, logs, journal, database, or UI. | Hard invariant | Secret scanning in pre-commit and CI; log-redaction tests |
| NFR-SEC-02 | The control socket authenticates by peer credentials and a token. The UI server binds to localhost only and enforces CSRF protection. | Hard invariant | Security tests |
| NFR-SEC-03 | Dependencies are scanned for known vulnerabilities in CI. | Zero unaccepted high-severity findings | CI |
| NFR-OBS-01 | Structured logs; per-stage latency metrics; events correlated by journal sequence number | — | Review in Phase 3 |
| NFR-REP-01 | Replaying the same journal with the same profile produces byte-identical order-intent output | Hard invariant | Replay parity test |
| NFR-MNT-01 | Every change to a safety-critical path has human review | Enforced by code owners | CI branch protection |

## 9. Safety Requirements

These are absolute. They take precedence over every functional requirement.

1. **No live orders without a human gate.** Live mode requires a promotion file that a human has signed.
2. **Fail closed.** Unknown state, stale data, failed reconciliation, failed authentication, or a failed health check stops new entries.
3. **Reduction is always allowed.** When possible, the system must still permit orders that reduce risk, even while entries are blocked.
4. **The kill switch works without the strategy layer.** The watchdog and exchange-side paths do not depend on strategy code.
5. **No silent recovery.** After a halt, the system returns to trading only through reconciliation and an explicit operator resume.
6. **Limits are not relaxed by code.** Agents and strategies cannot change limit values.

## 10. Assumptions and Dependencies

| ID | Assumption or dependency | Impact if false |
|---|---|---|
| A-1 | Delta testnet behaves like production for order lifecycle, aside from liquidity and latency | Paper-to-live gap larger than expected; more time in Phase 6 |
| A-2 | Delta public and private WebSocket feeds provide sequence information or enough data to detect gaps | Gap detection relies on snapshots; more REST load |
| A-3 | The host has stable NTP/chrony synchronization | Time-based signals are unreliable; system halts on skew (AL-10) |
| A-4 | A historical data source with usable quality exists for Indian derivatives and commodities | Phase 2 backtests are limited to crypto; Indian strategies wait |
| A-5 | The operator supplies production risk parameters, capital allocation, and instrument specifications | Phase 6 and 7 cannot start |
| A-6 | Codex and Cursor both read the repository's `AGENTS.md` | Agents need the rules pasted into each session |

## 11. Risks

| ID | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R-01 | Groww's unofficial API changes without notice, breaks, or is restricted | High | High | Adapter behind a build flag; system does not depend on Groww for safety; official-API review in §12 |
| R-02 | Regulatory requirements for algorithmic trading are not met | Medium | Severe | Compliance gate in §12 before any live trading; compliance records in `docs/compliance/` |
| R-03 | Strategy overfitting to historical data | High | High | Walk-forward testing; out-of-sample holdout; paper trading phase |
| R-04 | Poor market data quality produces false signals | Medium | High | Data quality checks; staleness and gap detection; replay validation |
| R-05 | Agent-written code introduces defects in safety-critical modules | Medium | Severe | Code owner review on Class A paths; sanitizers; chaos and parity tests; no agent auto-merge |
| R-06 | Host, network, or power failure during open positions | Medium | High | Exchange-side dead-man switch; watchdog; reconciliation on restart; UPS recommended |
| R-07 | Operator error (wrong profile, wrong limits, bad promotion) | Medium | High | Named profiles; promotion file; tighten-only UI; audit log |
| R-08 | Strategy logic causes unbounded loss | Medium | Severe | Hard risk gate outside strategy code; daily loss and drawdown kill switches |
| R-09 | AI agent quota exhaustion leaves code in an inconsistent state | High | Medium | Checkpoint commits; PROGRESS.md handoff protocol; resume prompt verifies repository state |
| R-10 | API key or TOTP seed compromise | Low | Severe | Keyring storage; IP allowlisting where offered; key rotation schedule; no secrets in repo |
| R-11 | Backtest/live divergence from fill-model error | High | High | Fill-model calibration; paper-vs-model comparison; tolerance thresholds in TESTING_QA.md §8 |

## 12. Gates Before Live Trading

Each gate must be recorded in `docs/compliance/` or `docs/adr/` with a date and an owner.

| Gate | Description | Blocks | Owner |
|---|---|---|---|
| G-LIVE-1 | Official trading-API status for Groww confirmed, or Groww live trading formally excluded | Enabling `TRADING_ENABLE_GROWW` | Operator |
| G-LIVE-2 | Algorithmic-trading registration or approval requirements for each venue confirmed with the broker and, where required, the exchange. Requirements must be checked against current SEBI and exchange rules. | Any live order | Operator with compliance advisor |
| G-LIVE-3 | Static IP whitelisting configured where the broker requires it | Any live order from that broker | Operator |
| G-LIVE-4 | Market data licensing and redistribution terms reviewed | Storing or sharing datasets | Operator |
| G-LIVE-5 | Production risk parameters approved and recorded in an ADR | Phase 7 | Operator |
| G-LIVE-6 | Phase 6 paper-trading exit criteria met (TESTING_QA.md §9) | Phase 7 | Operator |
| G-LIVE-7 | Signed promotion file created for the release tag and config hash | Live profile start | Operator |

## 13. Milestones

Week numbers are from the start of Phase 0. Exit criteria are defined in PROCESS_PROJECT_MANAGEMENT.md §6.

| Phase | Name | Weeks | Outcome |
|---|---|---|---|
| 0 | Foundation | 1–2 | Repository, CI, schemas, database, profiles, and core primitives exist and pass tests |
| 1 | Data infrastructure | 3–4 | Live tick capture to the journal; data quality reporting |
| 2 | Strategy library and backtester | 5–8 | Backtester uses the shared strategy library; walk-forward results reproducible |
| 3 | Live engine core | 9–12 | Engine runs in replay mode with full risk gate; decisions match backtest |
| 4 | Broker adapters against simulators | 13–15 | Delta adapter passes testnet tests; Groww adapter passes mock tests |
| 5 | Chaos and failure testing | 16–17 | Every failure scenario reaches a documented safe state |
| 6 | Paper trading on live data | 18–22 (minimum 4 weeks) | Paper results within tolerance of backtest |
| 7 | Limited live deployment | 23 onward | One instrument, minimum size, tight limits, after all gates |
| 8 | Operations and monitoring | Continuous from Phase 5 | Dashboard, alerts, daily reports, and on-call procedures in use |

## 14. Success Metrics

| Phase | Metric | Target |
|---|---|---|
| 0 | Clean clone builds and passes CI | 100% |
| 1 | Continuous tick capture | ≥ 2 weeks without unexplained gaps; gap rate reported daily |
| 2 | Backtest reproducibility | Same config hash and data snapshot produce identical results |
| 3 | Replay decision parity | 100% identical order intents on the fixture set |
| 4 | Broker adapter test pass rate | 100% of failure-injection tests |
| 5 | Chaos scenarios reaching safe state | 100% of scenarios in TESTING_QA.md §6 |
| 6 | Paper vs. backtest divergence | Within tolerance (TESTING_QA.md §8) |
| 7 | Live incidents | Zero S0 or S1 incidents per month |
| 7 | Reconciliation | Zero unresolved mismatches at end of each session |

## 15. Open Questions

| ID | Question | Blocking | Owner |
|---|---|---|---|
| Q-1 | Does Groww provide an official trading API suitable for automated order placement? | G-LIVE-1; Groww adapter work | Operator |
| Q-2 | Which algorithmic-trading registration path applies to this account and these venues? | G-LIVE-2 | Operator |
| Q-3 | Which historical data source covers Indian derivatives and commodities at the required quality and licence? | Phase 2 for Indian instruments | Operator |
| Q-4 | What capital allocation and risk budget apply to Phase 7? | G-LIVE-5 | Operator |
| Q-5 | Which logging library is used (see ADR candidates in TECHNICAL_IMPLEMENTATION.md §7)? | Phase 0 T-0005 | Implementation agent proposes, operator approves |

Until a question is resolved, the affected work proceeds only in its non-blocked form. Agents record open questions in PROGRESS.md; they do not assume answers.
