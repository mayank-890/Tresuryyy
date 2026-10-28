# Testing and Quality Assurance Document

## Automated Derivatives & Commodities Trading System

| Field | Value |
|---|---|
| Document ID | QA-001 |
| Version | 0.1 |
| Related | PRD.md (requirements being verified), DESIGN_ARCHITECTURE.md (behavior being tested), PROCESS_PROJECT_MANAGEMENT.md (gates) |

---

## 1. Principles

1. **Tests are safety evidence.** A passing test suite is the basis for trusting a change to a path that can move money. Tests are never weakened to make a build pass.
2. **Test the failure paths first.** Most losses come from timeouts, reconnects, partial fills, and restarts. Happy-path tests are necessary but not sufficient.
3. **Parity is measured, not assumed.** Backtest, replay, paper, and live are compared with explicit metrics and thresholds.
4. **Every requirement has a test.** Traceability runs from PRD requirement to test to evidence (§11).
5. **Fail closed in the tests too.** A test that cannot run (missing fixture, failing environment) fails. It does not pass silently.

## 2. Test Levels

| Level | Scope | Tools | Runs | Blocking |
|---|---|---|---|---|
| L1 Unit | Single functions and classes | GoogleTest; pytest | Every commit | Yes |
| L2 Property-based | Invariants over generated inputs (state machine, limits, sizing) | RapidCheck (C++); hypothesis (Python) | Every PR | Yes |
| L3 Fuzz | Parsers (WebSocket frames, REST responses, journal records, config) | libFuzzer; Atheris | Smoke on PR (60 s); long nightly | Smoke: yes |
| L4 Integration | Multiple components in-process, against the mock exchange | GoogleTest; pytest | Every PR | Yes |
| L5 Replay parity | Recorded data through the live engine; decisions compared to reference | Custom harness (`just replay-parity`) | PR touching strategy, risk, orders, journal; nightly | Yes |
| L6 Chaos | Injected faults across processes and the broker simulator | Chaos harness (`tests/chaos/`) | Nightly; before each phase exit | Nightly: alerts; phase exit: yes |
| L7 Soak | Long-running paper or testnet operation | Operator-run with monitoring | Phases 4, 6 | Yes at phase exit |
| L8 End-to-end (UI) | Dashboard and control panel against running stack | Playwright | Every PR touching `ui/` or `services/control_api` | Yes |
| L9 Performance | Latency, allocation, throughput | Google Benchmark; allocation hook | Nightly; Phase 3 gate | Regression >10%: alerts; Phase 3 budget: yes |
| L10 Migration | Schema apply, rollback, and upgrade from previous schema | dbmate in CI | Every PR touching `db/` | Yes |
| L11 Compatibility | Journal and control protocol readers against stored old versions | Fixture-based tests | Every PR touching `schemas/` or `proto/` | Yes |
| L12 Security | Secret scanning, dependency scanning, control-socket authentication, log redaction | gitleaks; vulnerability scanner; custom tests | PR and nightly | Yes |
| L13 Manual | Pre-live checklist, drills, review of unusual output | Human | Phases 5–7 | Yes |

## 3. Engine Test Specifications

### 3.1 Market Data

| ID | Test | Expected result |
|---|---|---|
| TC-MD-01 | Normalize each supported Delta message type from fixtures | Field-by-field equality with expected `Quote` and `Trade` |
| TC-MD-02 | Malformed frames (truncated, wrong types, oversized, invalid UTF-8) | Rejected with `MalformedFrame`; counter incremented; no crash; no state change |
| TC-MD-03 | Sequence gap inserted | Feed enters RESYNCING; orders blocked; snapshot requested; HEALTHY only after snapshot |
| TC-MD-04 | No update for longer than threshold during session | Instrument STALE; RL-15 blocks orders |
| TC-MD-05 | Update arrives after STALE | Returns to HEALTHY only after the feed's health rules pass |
| TC-MD-06 | Session closed | No STALE raised; no orders accepted (calendar test) |
| TC-MD-07 | Time-to-first-quote after resync | Measured and asserted within the profile's limit |
| TC-MD-08 | Fuzz: random bytes into the frame parser for 60 s | No crash, no sanitizer findings, no unbounded memory growth |

### 3.2 Order State Machine

| ID | Test | Expected result |
|---|---|---|
| TC-OS-01 | Every transition in DESIGN_ARCHITECTURE.md §5.2 | Accepted; correct resulting state; journaled |
| TC-OS-02 | Every (state, event) pair not in the table | Rejected as `InvalidTransition`; defect raised; scope halted |
| TC-OS-03 | Property test: random event sequences for 10,000 orders | Final state always terminal or UNKNOWN; no order in two states; counts conserved |
| TC-OS-04 | Fill arrives after cancel request | Final state is PARTIALLY_FILLED or FILLED; never CANCELLED with a larger filled quantity |
| TC-OS-05 | Duplicate fill ID | Applied once; duplicate counted |
| TC-OS-06 | Out-of-order events (ack after fill) | Handled per table; missing ack reconstructed from fill; logged |

### 3.3 Risk Gate

Every limit in DESIGN_ARCHITECTURE.md §7 has a boundary test matrix: for each limit, test **below**, **exactly at**, and **above** the threshold, plus reduce-only behavior where it applies.

| ID | Limit | Boundary cases | Additional cases |
|---|---|---|---|
| TC-RK-01 | RL-01 Notional | Below, at, above | Overflow-prone large values; zero price |
| TC-RK-02 | RL-02 Quantity | Below, at, above | Zero quantity; negative quantity rejected as invalid input |
| TC-RK-03 | RL-03 Net position per instrument | Below, at, above | Reduction allowed when above; reduction that flips sign |
| TC-RK-04 | RL-04 Net position per underlying | Below, at, above | Multiple instruments on one underlying |
| TC-RK-05 | RL-05 Gross exposure | Below, at, above | Long and short combinations |
| TC-RK-06 | RL-06 Greeks | Each Greek: below, at, above | Missing Greeks blocks orders |
| TC-RK-07 | RL-07 Collar | Just inside, at, just outside | Stale reference price; zero reference price |
| TC-RK-08 | RL-08 Rate budget | Budget exhausted; cancels still pass | Priority ordering under load |
| TC-RK-09 | RL-09 Cancel ratio | Below, at, above window threshold | Window rollover |
| TC-RK-10 | RL-10 Daily loss | Below, at, above | Loss recalculated after fill; resets at session start |
| TC-RK-11 | RL-11 Drawdown | Below, at, above | Global kill fires once; subsequent events do not re-fire |
| TC-RK-12 | RL-12 Self-trade | Crossing own resting order; not crossing | Same-price resting orders |
| TC-RK-13 | RL-13 Margin | Post-trade above, at, below buffer | Stale margin data blocks entries; reductions allowed |
| TC-RK-14 | RL-14 Expiry day | Final session, prior session | Time-zone boundary |
| TC-RK-15 | RL-15 Stale | STALE instrument blocks; other instruments unaffected | — |
| TC-RK-16 | RL-16 Reduce-only | Reductions allowed; increases rejected | Reduction that would overshoot to opposite side |
| TC-RK-17 | Tighten-only | Tighten applies; loosen attempt rejected | Tighten persists across restart |
| TC-RK-18 | Approval token | Missing, expired (>500 ms), mismatched client ID rejected at T4 | Reused token rejected |

### 3.4 Journal

| ID | Test | Expected result |
|---|---|---|
| TC-JR-01 | Write, close, read back N records | Byte-identical round trip |
| TC-JR-02 | Torn tail (truncate mid-record) | Detected; truncated; AL-14 raised; prior records intact |
| TC-JR-03 | CRC corruption mid-segment | Engine halts; no records skipped silently |
| TC-JR-04 | Sequence gap | Detected; halts |
| TC-JR-05 | Group fsync timing | Durable sequence published after fsync; sender waits |
| TC-JR-06 | Segment rotation at size limit | Footer written; next segment starts at correct sequence |
| TC-JR-07 | Disk full during write | Engine halts; no partial record considered durable |
| TC-JR-08 | Kill -9 during write storm (100 runs) | Recovery finds consistent prefix every run |
| TC-JR-09 | Unknown record type | Skipped by reader; counted |
| TC-JR-10 | Records from `v1` fixtures | Readable by current build (compatibility) |

### 3.5 Hot-Path Allocation and Performance

| ID | Test | Budget |
|---|---|---|
| TC-PF-01 | Allocation count across 1,000,000 simulated ticks through T1–T3 after warm-up | Zero allocations |
| TC-PF-02 | Decision latency (frame received → risk decision emitted), 10-minute replay at production-like rate | p99 ≤ 250 µs (NFR-PERF-01) |
| TC-PF-03 | Journal append latency, excluding fsync window | p99 ≤ 50 µs (NFR-PERF-03) |
| TC-PF-04 | SPSC ring throughput and latency | Recorded baseline; regression >10% fails nightly |
| TC-PF-05 | Parser throughput | Recorded baseline; regression >10% fails nightly |

Measurements run on the pinned host profile (`deploy/host/`) with the same isolated cores used in production. Results from laptops are informational only.

### 3.6 Threading

| ID | Test | Tool |
|---|---|---|
| TC-TH-01 | All SPSC ring users under TSan with randomized scheduling | TSan preset |
| TC-TH-02 | Shutdown ordering: all threads join; no leaked resources | ASan + custom check |
| TC-TH-03 | Control command racing with market data | Stress test, 1 hour nightly |
| TC-TH-04 | Heartbeat under CPU saturation on other cores | Integration |

### 3.7 Schema and Protocol Compatibility

- Fixtures for every released journal format and control protocol version are kept in `tests/fixtures/compat/<version>/`.
- Every PR runs the current readers against all fixtures.
- Changing a fixture is Class A and requires an ADR explaining why the old data is no longer valid.

### 3.8 Configuration

| ID | Test | Expected |
|---|---|---|
| TC-CF-01 | Profile missing name | Startup fails |
| TC-CF-02 | Unknown key | Startup fails |
| TC-CF-03 | Live profile without promotion file | Startup fails; AL-13-equivalent event recorded |
| TC-CF-04 | Promotion file with wrong config hash | Startup fails |
| TC-CF-05 | Promotion file with bad signature | Startup fails |
| TC-CF-06 | Promotion file expired | Startup fails |
| TC-CF-07 | Config hash written to journal on start | Present and matches |
| TC-CF-08 | Groww adapter referenced in a profile while build flag OFF | Startup fails with clear error |
| TC-CF-09 | Secret referenced by name but not present | Startup fails; value never printed |

## 4. Broker Adapter Test Specifications

### 4.1 Common (All Adapters)

| ID | Scenario | Expected |
|---|---|---|
| TC-BR-01 | Successful place, ack, fill | Correct state sequence; fill applied once |
| TC-BR-02 | Definitive reject | REJECTED; reason recorded; no retry |
| TC-BR-03 | Timeout after send, order exists at broker | UNKNOWN → query → matching state |
| TC-BR-04 | Timeout after send, order does not exist | Query ×3 → same-ID resend → recorded outcome (DESIGN §5.4) |
| TC-BR-05 | Duplicate client ID rejected by broker on resend | Treated as found; state reconciled; no second position |
| TC-BR-06 | 429 response | Category backoff; cancels continue; entries queued |
| TC-BR-07 | 5xx burst | Retry with backoff; no duplicate orders |
| TC-BR-08 | Authentication failure | Fatal; adapter halted; AL-12 |
| TC-BR-09 | Connection lost with resting orders | Feed DOWN; orders blocked; reconcile on reconnect |
| TC-BR-10 | Reconcile mismatch injected (broker shows extra fill) | Mismatch recorded; scope halted; AL-03 |

### 4.2 Delta Testnet

| ID | Scenario | Expected |
|---|---|---|
| TC-DL-01 | HMAC signing vector from documentation | Signature matches exactly |
| TC-DL-02 | Clock skew beyond threshold | Session refuses to start |
| TC-DL-03 | Private WebSocket heartbeat missing | Reconnect; REST reconciliation before resume |
| TC-DL-04 | Public WebSocket sequence gap | Snapshot resync |
| TC-DL-05 | Dead-man-switch refresh | Refreshed on schedule; expiry tested by pausing refresh on testnet |
| TC-DL-06 | Testnet soak ≥ 3 days | No unexplained mismatches; all alerts explained |

### 4.3 Groww (Build-Flagged)

| ID | Scenario | Expected |
|---|---|---|
| TC-GW-01 | Adapter compiled only when flag ON | Symbol absent from OFF build (checked by `nm`/symbol test) |
| TC-GW-02 | Pre-open re-authentication failure | Cancel-only; AL-12 |
| TC-GW-03 | TOTP generation uses seed from secret store only | No seed in logs or environment (log scan) |
| TC-GW-04 | Rate limit at configured share of observed limit | Requests paced; priority honored |
| TC-GW-05 | Session expiry mid-day | Cancel-only; reconcile on re-auth |

Groww tests run against the mock adapter in CI. No test contacts a Groww endpoint.

## 5. Replay Parity

### 5.1 Definition

Replay parity holds when, for a fixed journal input, fixed profile hash, and fixed engine build, the sequence of **order intents** and **risk decisions** produced by the live engine in replay mode is identical to the reference sequence stored in `tests/replay/reference/`.

Comparison is exact: same client IDs, instruments, sides, quantities, prices, decision outcomes, limit IDs, and ordering. Timestamps are excluded from comparison and checked separately for monotonicity.

### 5.2 Reference Sets

| Set | Content | Source |
|---|---|---|
| `fixtures-small` | Synthetic scenarios covering each strategy and each risk limit | Generated and committed |
| `fixtures-recorded-N` | Recorded real ticks (sanitized, licence-checked) | Phase 1 captures, operator-approved |
| `fixtures-adversarial` | Gaps, bursts, halts, crossed markets, zero-volume periods | Synthetic |

Reference decisions are regenerated only through a Class A PR with an ADR explaining the intended change in behavior. A parity failure is never resolved by regenerating references without that explanation.

### 5.3 Backtest-to-Live Comparison

| Metric | Definition | Tolerance (proposed; operator confirms in Phase 2) |
|---|---|---|
| Decision agreement | Share of signal events where backtest and replay agree on direction and size | 100% (within replay) |
| Fill rate | Filled / submitted, per instrument | Within ±10 percentage points of model |
| Median slippage | Fill price vs. reference price, in ticks | Within ±2 ticks or ±20% of model, whichever is larger |
| PnL per day | Realized + unrealized | Within the confidence band from walk-forward; reported with the band |
| Position at session end | Net positions | Exact match after reconciliation |

Tolerances live in `config/qa/tolerances.toml`. Changing a tolerance is a Class B change with a recorded reason.

## 6. Chaos Scenario Catalog

Each scenario is run in the chaos harness with a defined expected safe state. A scenario passes when the system reaches that state within the stated time and passes reconciliation afterwards.

| ID | Scenario | Injected fault | Expected safe state | Max time to safe state |
|---|---|---|---|---|
| CH-01 | Network cut mid-order (order sent, response lost) | Drop response packets | UNKNOWN → resolved by query; no duplicate | 15 s |
| CH-02 | Engine kill during order send | SIGKILL `trader` between journal and send | On restart: intent found, state resolved, no duplicate | 60 s recovery |
| CH-03 | Engine kill with resting orders | SIGKILL with open orders | Watchdog cancels all; flatten if configured | 10 s |
| CH-04 | Watchdog kill | SIGKILL `watchdog` | `trader` halts new orders; alerts; exchange dead-man switch covers host if engine also stops | 5 s |
| CH-05 | Full host reboot | Power-cycle VM | Exchange cancels resting orders via dead-man switch; restart → reconcile → resume after operator clear | 60 s to ready |
| CH-06 | Database down for 30 min | Stop PostgreSQL | Trading continues; projector lag AL-11 at 5 min; catches up | Immediate for trading |
| CH-07 | Private feed dropped while orders rest | Kill WebSocket | DEGRADED; entries blocked; reconcile on reconnect | 10 s |
| CH-08 | Clock skew injected (+2 s) | Adjust host time offset | Time-based signals blocked; AL-10 | 5 s |
| CH-09 | Feed stall on one instrument | Stop updates | STALE; orders on instrument blocked; others continue | 3 s |
| CH-10 | 429 storm | Broker returns 429 for 60 s | Category backoff; cancels continue; no ban | Continuous |
| CH-11 | Journal disk full | Fill disk | HALTED; no unjournaled orders | Immediate |
| CH-12 | Torn journal tail on restart | Truncate last record | AL-14; recovery; reconcile | 60 s |
| CH-13 | Exchange maintenance window | Broker returns maintenance | Cancel-only before window; halt during | Per calendar |
| CH-14 | Authentication revoked mid-session | Invalid key | Fatal; halt; AL-12 | 5 s |
| CH-15 | Reconciliation mismatch | Broker shows unexpected position | Scope halted; AL-03; operator clear required | 60 s |
| CH-16 | Operator kill while orders in flight | Kill command during send | Global halt; cancel all; no new sends after command journaled | 5 s |
| CH-17 | Two engine instances for one profile | Start second `trader` | Second refuses (lock lease) | Immediate |
| CH-18 | Strategy throws during decision | Injected exception path (test build only) | Strategy halted; engine continues; defect recorded | Immediate |

Chaos tests run nightly in the `chaos` environment. Before each phase exit, all scenarios run three times.

## 7. Paper and Testnet Soak Testing

### 7.1 Soak Requirements

| Phase | Environment | Minimum duration | Required results |
|---|---|---|---|
| 4 | Delta testnet | 3 days continuous | No unexplained mismatches; every alert explained; no S0/S1 |
| 6 | Paper (live data) | 4 weeks, covering at least 3 different market regimes if the data allows | Divergence within §5.3 tolerances; daily reports complete |

### 7.2 Daily Soak Checks (Operator)

- Daily report generated and reviewed
- Reconciliation clean at session end
- Count of UNKNOWN orders is zero, or each one is resolved and documented
- No unexplained STALE events during market hours
- Journal size and disk headroom within limits
- Alert log reviewed

## 8. Acceptance Thresholds

| Area | Threshold | Enforcement |
|---|---|---|
| Line coverage, `engine/src/risk`, `orders`, `journal` | ≥ 95% | CI report; fails below |
| Branch coverage, `engine/src/risk/limits` and state machine | 100% of transitions in DESIGN §5.2 and all RL limits | Traceability check |
| Line coverage, other engine modules | ≥ 85% | CI report |
| Line coverage, `research/src/tradelab` | ≥ 80% | CI report |
| Line coverage, `services/` | ≥ 80% | CI report |
| Sanitizer findings | Zero | CI |
| TSan findings | Zero | CI |
| Fuzz crashes | Zero open | CI and nightly |
| High-severity dependency vulnerabilities | Zero unaccepted | CI |
| Secret scan findings | Zero | Pre-commit and CI |
| Replay parity failures | Zero | CI |
| Benchmark regression | ≤ 10% | Nightly |

Coverage is a floor, not evidence of quality. Boundary matrices (§3.3) and state tables (§3.2) are the primary evidence for safety-critical code.

## 9. Phase Exit Test Gates

| Phase | Required test evidence |
|---|---|
| 0 | L1, L4 (basic), L10, L12 pass. Journal inspector fixtures pass (TC-JR-02, TC-JR-10 at least). Profile tests TC-CF-01 to TC-CF-09 pass. |
| 1 | Capture runs ≥ 2 weeks; TC-JR-08 passes 100 runs; data quality report produced |
| 2 | Backtest reproducibility test; walk-forward test; fill-model unit tests; pybind11 parity with Python reference |
| 3 | All TC-MD, TC-OS, TC-RK, TC-JR, TC-PF tests pass; TC-TH under TSan; replay parity on `fixtures-small` and `fixtures-adversarial`; latency measured against NFR-PERF-01 |
| 4 | All TC-BR-01 to TC-BR-10; TC-DL-01 to TC-DL-06; TC-GW-01 to TC-GW-05; fuzz smoke; Delta testnet soak complete |
| 5 | All CH-01 to CH-18, each three times; watchdog activation latency measured |
| 6 | Paper soak complete; tolerance report; weekly drill records; daily reports |
| 7 | Live smoke test checklist (§12) complete; first-session review complete |
| 8 | Alert drill records for each AL-## in USER_OPERATIONS.md §8 |

## 10. CI Required Checks

Required on every PR to `main`:

1. `build-debug-asan`: build and unit + integration tests
2. `build-debug-ubsan`: build and unit tests
3. `tsan-threaded`: TSan on threaded modules (required for changes to `core`, `marketdata`, `journal`, `control`, `strategy`, `risk`)
4. `header-self-contained`
5. `dependency-rules`: include-graph and Python dependency rules (DESIGN §3 and TECHNICAL §3.3)
6. `watchdog-link-restriction`
7. `fuzz-smoke`: 60 seconds per target
8. `python-lint-types-tests`
9. `ui-lint-playwright`
10. `migrations-apply-down`
11. `codegen-drift`
12. `journal-compat`
13. `replay-parity`: required when touching strategy, risk, orders, journal, or marketdata
14. `secret-scan`
15. `dependency-vulnerability-scan`
16. `coverage-thresholds`

Nightly: full fuzz (30 minutes per target), chaos suite, benchmark regression, dependency rescan, full replay on recorded fixtures.

## 11. Traceability

Every requirement ID in PRD.md maps to at least one test ID. The mapping is kept in `tests/traceability.csv` with columns: `requirement_id`, `test_ids`, `evidence_location`, `status`.

A CI check fails when:
- a MUST requirement has no mapped test, or
- a mapped test ID does not exist, or
- a test is mapped to a requirement that was removed without a note.

Sample rows:

| requirement_id | test_ids | Evidence |
|---|---|---|
| FR-OR-04 | TC-BR-03, TC-BR-04, TC-BR-05, CH-01 | CI artifact; chaos report |
| FR-OR-01 | TC-JR-05, TC-OS-01, CH-02 | CI artifact; chaos report |
| FR-RK-01 | TC-RK-01 to TC-RK-18 | CI artifact |
| FR-RK-02 | TC-RK-17, TC-CF-* (compile-time check) | CI artifact |
| FR-RK-03 | CH-03, CH-04, CH-16 | Chaos report; drill record |
| FR-JR-02 | TC-JR-02, TC-JR-03, CH-12 | CI artifact |
| FR-MD-03 | TC-MD-03, TC-DL-04 | CI artifact; soak report |
| FR-PF-02 | TC-BR-10, CH-07, CH-15 | Chaos report |
| NFR-REP-01 | §5 replay parity | CI artifact |
| NFR-SEC-01 | Secret scan; TC-GW-03; log-redaction test | CI artifact |

## 12. Pre-Live Checklist (Phase 7 Gate)

All items must be checked and signed by the operator. Stored in `docs/reviews/pre-live-<tag>.md`.

- [ ] Release tag built; manifest verified
- [ ] Promotion file created, signed, and matches config hash and tag
- [ ] Paper soak report reviewed; divergence within tolerance
- [ ] All CH scenarios passed in the most recent phase-exit run
- [ ] Kill drill completed within the 7 days before go-live: software path, watchdog path, exchange-side path
- [ ] Risk parameters recorded in ADR-0012 and reviewed
- [ ] Instrument specifications for the live instrument verified against the exchange
- [ ] Static IP whitelisting confirmed (G-LIVE-3)
- [ ] Regulatory gates G-LIVE-1, G-LIVE-2 recorded
- [ ] Secrets rotated after paper-to-live transition; old keys revoked
- [ ] Alerts delivered to the operator's device in a test
- [ ] Daily report dry run completed
- [ ] Runbooks reviewed: broker outage, reconciliation mismatch, engine crash, host failure
- [ ] Capital limit configured at the minimum value
- [ ] Operator has reviewed the first-session plan (USER_OPERATIONS.md §4.4)

## 13. Defect Management

### 13.1 Severity

| Severity | Definition | Examples | Response |
|---|---|---|---|
| S0 | Money at risk from a defect in production, or a safety invariant broken | Duplicate order sent; risk limit bypassed; secret exposed | Stop trading immediately; kill switch; incident record within 24 h |
| S1 | Correctness defect that could lead to S0 under plausible conditions, or a safety gate not operating | Replay parity failure; watchdog not activating in test; reconciliation skipped | Blocks merge and release; fixed before next phase |
| S2 | Incorrect behavior without money risk, or degraded function with a workaround | Wrong PnL display; UI status delayed; a test flaking | Fixed within the current phase |
| S3 | Cosmetic or documentation defect | Typos; layout issues | Scheduled |

### 13.2 Bug Handling Rules

- Every bug fix includes a regression test that fails before the fix and passes after.
- A flaky test is treated as a defect. It is quarantined only with an approved exception, a linked ticket, and a deadline. A quarantined test still runs and is reported.
- Escaped defects (found after merge) are recorded with root cause and the test that would have caught them.

## 14. Test Environments

| Environment | Purpose | Data | Credentials | Network to venues |
|---|---|---|---|---|
| `dev` | Unit and integration tests | Synthetic fixtures | None | None |
| `ci` | Automated gates | Synthetic and small recorded fixtures | None | None |
| `chaos` | Fault injection | Synthetic; mock exchange | None | None |
| `testnet` | Delta testnet | Testnet market | Testnet keys | Delta testnet only |
| `paper` | Paper on live data | Live market data | None | Market data only, read-only |
| `live` | Production | Live | Live keys | Live venues (gated) |

Development and CI never hold live credentials. `paper` holds market-data credentials only, if the venue requires them.

## 15. Manual Testing

- UI manual checklist (`docs/qa/ui-checklist.md`) run before each phase exit: status indicators, every control's confirmation dialog, kill button, refresh behavior, responsive layout at 1280 px and 1920 px widths.
- Operator drills (USER_OPERATIONS.md §6) recorded in `docs/reviews/drills/`.
- Exploratory review of unusual states: each new alert and each new state name tested by hand once.

## 16. Test Data Management

- Synthetic fixtures are generated by a seeded script in `tests/fixtures/generators/`. Output is deterministic and committed only when small (under 1 MB per file).
- Recorded market data is stored outside git under the data root. The repository stores its checksum and a manifest.
- Sanitization: remove account identifiers, IP addresses, and any field not required for replay.
- Licence check for recorded data is recorded in the manifest (PRD §12, G-LIVE-4).
- No real account data in any fixture.

## 17. Roles in QA

| Role | Responsibility |
|---|---|
| Implementation agent | Writes tests with each change; runs `just check`; never weakens tests |
| Independent review pass (§PM 5.5) | Checks tests against the spec for Class A changes |
| Operator | Reviews Class A test results; approves phase exits; runs manual checklists; approves pre-live checklist |
