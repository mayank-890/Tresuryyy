# Design and Architecture Document

## Automated Derivatives & Commodities Trading System

| Field | Value |
|---|---|
| Document ID | DA-001 |
| Version | 0.1 |
| Related | PRD.md (requirements), TECHNICAL_IMPLEMENTATION.md (how to build), TESTING_QA.md (how to verify) |

---

## 1. Architectural Principles

1. **The journal is the source of truth. The database is a projection.** Every state change is written to an append-only local journal before it takes effect outside the process. PostgreSQL is rebuilt from the journal and can be lost without losing trading state.
2. **Python is never on the order path.** The C++ engine decides, sizes, risk-checks, and sends orders. Python does research, persistence projection, and presentation.
3. **One strategy implementation, two hosts.** The strategy library is compiled into the live engine and into a pybind11 module used by research. Parity is enforced by construction, not by convention.
4. **Strategies cannot place orders.** They emit intents. Only the risk gate can approve them, and only the execution thread can send them.
5. **Fail closed.** Unknown state stops new entries. Reduction remains possible where the system can still reach the broker.
6. **Brokers are adapters.** No broker type crosses the strategy boundary.
7. **Time is explicit.** Every record carries monotonic time for ordering and latency, wall-clock time for audit, and exchange time when available. The three are never mixed.

## 2. System Context

```
                         ┌──────────────────────────────┐
  Operator (browser) ──▶ │ UI (static HTML/CSS/JS)      │
                         └──────────────┬───────────────┘
                                        │ HTTP on localhost, token + CSRF
                         ┌──────────────▼───────────────┐      ┌──────────────────┐
                         │ control_api (Python)         │◀────▶│ PostgreSQL       │
                         └──────────────┬───────────────┘      │ (projection)     │
                                        │ UDS control socket   └────────▲─────────┘
                                        │ (authenticated)               │ batched COPY
 ┌──────────────────────────────────────▼──────────────────┐            │
 │ trader (C++ engine process)                             │     ┌──────┴──────────┐
 │  market data · strategy · risk gate · execution ·       │────▶│ journal (files, │
 │  private feed · reconciler · journal writer · telemetry │     │ append-only)    │
 └──────┬───────────────────────────────┬──────────────────┘     └──────┬──────────┘
        │ WebSocket + REST              │ heartbeat (UDS)               │ read
  ┌─────▼─────┐                 ┌───────▼──────────┐           ┌────────▼────────┐
  │ Brokers   │                 │ watchdog (C++,   │           │ projector       │
  │ Delta     │◀── cancel-all ──│ separate user,   │           │ (Python)        │
  │ Groww*    │   flatten       │ own credentials) │           └─────────────────┘
  └───────────┘                 └──────────────────┘
  * Groww compiled only with TRADING_ENABLE_GROWW=ON
```

Research (`research/`) runs offline. It reads historical data and journal exports, and it uses the engine's strategy library through the pybind11 module. It has no connection to the live engine.

## 3. Process Model

| Process | Language | Runs as | Responsibility | Restart policy |
|---|---|---|---|---|
| `trader` | C++20 | `trading` user | Market data, decisions, risk, orders, reconciliation, journal | systemd, max 3 restarts per 10 min, then stays down and alerts |
| `watchdog` | C++20 | `trading-watchdog` user with separate credentials | Monitors `trader` heartbeat. Cancels all orders and flattens if heartbeat is lost. Links only broker REST, control client, and journal reader. | systemd, always on, independent of `trader` |
| `projector` | Python 3.12 | `trading` user | Reads journal, writes PostgreSQL idempotently | systemd, unlimited restarts with backoff |
| `control_api` | Python 3.12 | `trading` user | Serves the UI over localhost. Translates UI actions into control-socket commands. | systemd |
| `ui` | Static files | Served by `control_api` | Dashboard and control panel | — |
| `ingestion` | Python 3.12 | `trading` user | Tick recorder for paper and live capture; historical backfill | systemd (capture); manual (backfill) |

Only `trader` holds live trading credentials for normal operation. `watchdog` holds a separate, narrower credential that permits cancel and position-reduction only.

## 4. Engine Threading Model

Each thread is pinned to a dedicated core. Threads communicate through lock-free single-producer, single-consumer (SPSC) rings with cache-line-padded indices. The hot path contains no mutexes, no heap allocation after warm-up, and no I/O.

```
 Broker WS (public) ──▶ [T1 MarketData] ──ring A──▶ [T2 Strategy] ──ring B──▶ [T3 RiskGate]
                         normalize, seq check         signals, sizing           limits, throttle,
                         staleness stamp              param snapshot            approval token
                                                                                     │ ring C
 Broker WS (private) ──▶ [T5 PrivateFeed] ──ring E──▶ [T6 Reconciler] ◀─┐   [T4 Execution]
                         order updates, fills                           │    owns broker REST
                                                                        └── state ──▶ REST sockets
 [T7 JournalWriter] ◀── all threads push records to ring J (per-thread SPSC)
   group fsync; durable sequence numbers published
 [T8 ProjectorFeed] ◀── tails journal files (separate process or thread; off hot path)
 [T9 Telemetry]   samples state at 4 Hz, publishes to shared memory for control_api
 [T10 Control]    UDS server: halt, resume, cancel-all, tighten-limit, status
 [T11 Heartbeat]  writes heartbeat to watchdog every 500 ms; reads watchdog ack
 [T12 Logger]     drains lock-free log queue to disk
```

| Thread | Core affinity | Blocking allowed | Notes |
|---|---|---|---|
| T1 MarketData | Dedicated | No (except socket read via epoll) | Parses frames into preallocated pools |
| T2 Strategy | Dedicated | No | Pure computation over snapshots |
| T3 RiskGate | Dedicated | No | Deterministic; all limits checked here |
| T4 Execution | Dedicated | Socket send only | Sends orders only with a valid approval token |
| T5 PrivateFeed | Shared core OK | Socket read | Order updates and fills |
| T6 Reconciler | Shared core OK | No | Compares private feed to REST snapshots |
| T7 JournalWriter | Dedicated | fsync | Group commit; releases senders after durability |
| T9–T12 | Shared core OK | Yes | Off the decision path |

**Ordering guarantee.** Order intents are journaled and fsynced before T4 sends them. T4 waits for durability confirmation from T7 for each intent. This adds the group-commit window (≤ 2 ms by default) to order latency. The trade-off is deliberate: correctness over latency. See ADR-0002.

**Hot-path rules**
- Preallocated pools for ticks, orders, events, and journal buffers
- `steady_clock` for all latency measurement
- No `std::shared_ptr` on the hot path
- Atomics use explicit memory orders, documented at each use
- Logging through the lock-free queue only

## 5. Order Lifecycle

### 5.1 States

| State | Meaning | Terminal |
|---|---|---|
| `INTENT_JOURNALED` | Intent is durable locally; not yet sent | No |
| `PENDING_NEW` | Sent; no broker acknowledgment yet | No |
| `ACKED` | Broker accepted the order | No |
| `PARTIALLY_FILLED` | Some quantity filled | No |
| `FILLED` | Fully filled | Yes |
| `PENDING_CANCEL` | Cancel sent; no confirmation yet | No |
| `CANCELLED` | Cancelled by request or by exchange | Yes |
| `REJECTED` | Definitively rejected by the broker or the risk gate | Yes |
| `EXPIRED` | Expired by time-in-force | Yes |
| `UNKNOWN` | Outcome not known (timeout or ambiguous response) | No |

### 5.2 Transitions

| From | Event | To |
|---|---|---|
| `INTENT_JOURNALED` | Sent to broker | `PENDING_NEW` |
| `INTENT_JOURNALED` | Risk gate or local validation rejects | `REJECTED` |
| `PENDING_NEW` | Broker ack | `ACKED` |
| `PENDING_NEW` | Broker definitive reject | `REJECTED` |
| `PENDING_NEW` | Timeout or ambiguous response | `UNKNOWN` |
| `ACKED` | Partial fill | `PARTIALLY_FILLED` |
| `ACKED` / `PARTIALLY_FILLED` | Full fill | `FILLED` |
| `ACKED` / `PARTIALLY_FILLED` | Cancel requested | `PENDING_CANCEL` |
| `PENDING_CANCEL` | Cancel confirmed | `CANCELLED` |
| `PENDING_CANCEL` | Fill arrives before cancel | `PARTIALLY_FILLED` or `FILLED` |
| `ACKED` / `PARTIALLY_FILLED` | Exchange cancel or expiry | `CANCELLED` / `EXPIRED` |
| `UNKNOWN` | Query shows order exists | Matching state from query |
| `UNKNOWN` | Query shows no order, after the retry rule (§5.4) | `REJECTED` (with reason `NOT_FOUND_AFTER_QUERY`) |
| Any non-terminal | Transition not in this table | Invalid: log, raise defect, halt affected scope |

### 5.3 Client Order IDs

Format: `<session-prefix>-<monotonic counter>`, for example `S7F2-0000012345`.

- Session prefix is generated at engine start from a persisted counter, so IDs are unique across restarts.
- The counter is journaled, so IDs are never reused.
- IDs are passed to the broker as the client order identifier field wherever the broker supports it. The mapping from client ID to broker ID is journaled when the broker returns it.

### 5.4 Ambiguous Order Resolution

1. Mark the order `UNKNOWN`. Block new orders on the same instrument and strategy.
2. Query the broker by client ID (REST), up to 3 attempts with 1-second, then 3-second delays.
3. If found: transition to the reported state. Resume.
4. If not found after 3 queries: resend the identical intent with the **same** client ID, once. Brokers that enforce client-ID uniqueness reject the duplicate. Record the outcome.
5. If still unresolved after the resend: keep the instrument blocked, raise AL-04, and wait for operator action. Do not guess.

Under no circumstance is a new order with a new client ID used to resolve an `UNKNOWN` state.

## 6. Risk Gate

### 6.1 Position in the Pipeline

Strategy emits an `OrderIntent`. T3 evaluates it against all limits in §7. If approved, T3 issues an `ApprovalToken` bound to the intent's client ID, the limit-set version, and an expiry of 500 ms. T4 refuses any order without a valid, unexpired token whose client ID matches.

### 6.2 Design Rules

- Limits are `constexpr` values in `engine/src/risk/limits.hpp`. That is the only file where limit values are defined.
- Runtime configuration may tighten limits, never loosen them. Tightening takes effect on the next evaluation. Loosening requires a code change, review, and restart.
- The gate evaluates independently of strategy logic. A strategy's own checks are advisory.
- Every decision (approve or reject) is journaled with the limit ID, the observed value, and the threshold.
- The gate is deterministic: same inputs and state produce the same decision.

## 7. Risk Limits

| ID | Limit | Scope | Breach behavior |
|---|---|---|---|
| RL-01 | Max order notional | Per order | Reject |
| RL-02 | Max order quantity (lots) | Per instrument | Reject |
| RL-03 | Max net position | Per instrument | Reject entries; reductions allowed |
| RL-04 | Max net position | Per underlying | Reject entries; reductions allowed |
| RL-05 | Max gross exposure | Portfolio | Reject entries |
| RL-06 | Greeks limits: net delta, gamma, vega | Portfolio | Reject orders that increase breach |
| RL-07 | Price collar: distance from reference price (LTP or mid) | Per order | Reject if beyond collar |
| RL-08 | Order rate budget | Per broker, per endpoint category | Queue; cancels and reductions first |
| RL-09 | Cancel-to-order ratio over rolling window | Per strategy | Halt strategy |
| RL-10 | Daily loss (realized + unrealized) | Per strategy and portfolio | Halt strategy until next session |
| RL-11 | Drawdown from high-water mark | Portfolio | Global kill switch |
| RL-12 | Self-trade prevention | Per instrument | Reject |
| RL-13 | Post-trade margin headroom | Portfolio | Reject entries if below buffer; stale margin data blocks entries |
| RL-14 | Expiry-day restriction | Per contract | Block new entries in final session |
| RL-15 | Stale data | Per instrument | Block orders on the instrument |
| RL-16 | Reduce-only mode | Per strategy or global | Allow only reducing orders |

Numeric values are set by the operator in a reviewed ADR before Phase 7. During development, placeholder values are conservative paper-only values marked `TODO(operator)`.

## 8. Market Data Design

### 8.1 Normalization

Broker frames are parsed into `Quote` (bid, ask, sizes, exchange time, receive time) and `Trade` (price, size, side if provided, exchange time, receive time). Every field has a documented unit. Prices are integers in ticks; the instrument master converts to and from display values.

### 8.2 Health State Machine

| State | Entry condition | Effect |
|---|---|---|
| `HEALTHY` | Snapshot complete; sequence continuous; updates within threshold | Orders allowed |
| `STALE` | No update within threshold during session | Orders blocked (RL-15) |
| `RESYNCING` | Sequence gap detected or reconnect | Orders blocked; snapshot requested |
| `DOWN` | Socket lost; reconnect in progress | Orders blocked; cancels attempted if connection can be re-established |

### 8.3 Time

| Clock | Use |
|---|---|
| Monotonic (`steady_clock`) | Ordering, latency, staleness timers |
| Wall clock (UTC, ns) | Audit, reports, cross-process correlation |
| Exchange time | Reconciliation and signal timing where the broker provides it |

Clock-health monitor compares wall time against chrony. Skew above threshold raises AL-10 and blocks time-based signals.

## 9. Broker Abstraction

### 9.1 Interface

```
IBroker
  connect() / disconnect() / health() -> BrokerHealth
  subscribeMarketData(instruments)     -> stream<Quote | Trade>
  subscribePrivateFeed()               -> stream<OrderUpdate | Fill | PositionUpdate>
  placeOrder(OrderIntent)              -> SubmitResult { ACK | REJECT | AMBIGUOUS, brokerOrderId?, reason }
  cancelOrder(clientOrderId)           -> CancelResult
  cancelAll(scope)                     -> CancelAllResult
  queryOrder(clientOrderId)            -> OrderSnapshot | NotFound
  getPositions() / getOpenOrders() / getMargin()
  capabilities()                       -> { supportsIOC, supportsMarket, supportsClientOrderId,
                                            maxRatePerCategory, tickSizeRules, sessions }
```

### 9.2 Error Taxonomy

| Class | Examples | Action |
|---|---|---|
| Retryable | Network timeout, 5xx, 429 | Backoff; confirm state before retrying |
| Definitive reject | Insufficient margin, invalid price, instrument closed | Record REJECTED; continue |
| Ambiguous | Timeout after send, connection lost before response | Mark UNKNOWN; resolve per §5.4 |
| Fatal | Authentication failure, account restricted, key revoked | Halt the adapter; alert AL-12 |

### 9.3 Adapter Contracts

- **Delta adapter** (first live-capable venue): REST for order actions; private WebSocket for updates; public WebSocket for market data; HMAC-SHA256 signed requests; dead-man-switch timer where the API provides one.
- **Groww adapter** (gated): all methods behind `TRADING_ENABLE_GROWW`. Session lifecycle includes scheduled pre-open re-authentication. Any failure in re-authentication puts the adapter into cancel-only mode.
- **Mock adapter**: scriptable behavior for acks, fills, rejects, timeouts, 429s, and disconnects. Used in tests and paper mode.

### 9.4 Rate Limiting

Token buckets per endpoint category, sized to 50–60% of the observed or documented limit. Priority queue: cancels → reductions → new entries. A 429 backs off the entire category, not only the failing request.

## 10. Journal Design

### 10.1 Record Header

All integers are little-endian.

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | magic | `TJNL` |
| 4 | 2 | format version | Currently 1 |
| 6 | 2 | record type | Enumerated in `schemas/journal/v1` |
| 8 | 8 | sequence | Strictly increasing per journal |
| 16 | 8 | monotonic ns | Process-relative |
| 24 | 8 | wall ns (UTC) | Audit time |
| 32 | 4 | payload length | Bytes |
| 36 | 4 | CRC32C | Over header (with this field zeroed) and payload |
| 40 | n | payload | Type-specific |

Total fixed header: 40 bytes.

### 10.2 Files and Durability

- Segments of up to 256 MB, named by first sequence number.
- Group commit: writes are batched and fsynced at most every 2 ms (configurable) or when the batch reaches a size threshold.
- Each segment has a footer written on rotation (record count, last sequence, file CRC).
- The durable sequence number is published after fsync, and T4 waits on it before sending.

### 10.3 Recovery

1. Scan the newest segment forward, validating CRC and sequence continuity.
2. If a torn record is found at the tail, truncate to the last valid record and raise AL-14. Do not silently skip records in the middle; a mid-file CRC failure halts startup.
3. Rebuild order and position state by replaying records.
4. Compare with broker state (reconciliation). Trading does not resume until this is clean.

### 10.4 Compatibility

- Readers skip unknown record types using the payload length.
- New fields are added only in new record versions (`v2`). Existing versions are never altered.
- The compatibility rule is checked in CI (TESTING_QA.md §3.7).

## 11. Persistence Projection (PostgreSQL)

The projector reads journal records in sequence order and writes batches with `COPY`. Every projected row includes:

- `source_segment` — journal segment file identifier
- `source_sequence` — the record header `sequence` field (DESIGN §10.1)
- `event_time` — `timestamptz` converted from the record header `wall_ns` field (UTC audit time, §10.1 offset 24). The projector must not use `now()`, ingestion time, or any other clock for `event_time`.

Idempotent inserts use `ON CONFLICT DO NOTHING`:

- Non-partitioned projected tables: `(source_segment, source_sequence)`
- Range-partitioned append tables: `(source_segment, source_sequence, event_time)` (PostgreSQL requires the partition key in unique constraints; ADR-0013)

| Table | Content | Write pattern | Partitioning |
|---|---|---|---|
| `market_ticks` | Quotes and trades | Append via COPY | Daily range |
| `candles_1m` | Derived bars | Append | Daily range |
| `order_events` | Every order state change | Append | Daily range |
| `orders` | Current order state | Upsert by client ID | None |
| `fills` | Fills | Append | Monthly range |
| `position_snapshots` | Periodic and reconciliation snapshots | Append | Monthly range |
| `risk_events` | Every risk decision that rejected or modified an order, and every limit breach | Append | Monthly range |
| `control_commands` | Every operator action, with operator identity | Append | None |
| `config_versions` | Immutable config records keyed by hash | Insert once | None |
| `audit_log` | Security-relevant events | Append | Monthly range |

If PostgreSQL is unavailable, the projector buffers its position in the journal and resumes when the database returns. Trading is unaffected. Lag is reported as AL-11 after 5 minutes.

## 12. Control Plane

### 12.1 Interfaces

- **Control socket (UDS):** binary protocol defined in `proto/control/v1` (gRPC over UDS). Used by `control_api` and by the operator CLI.
- **Watchdog channel (UDS, separate):** heartbeat in both directions and the cancel-all command.
- **Exchange-side:** dead-man-switch timers.

### 12.2 Commands

| Command | Effect | Auth | Journaled |
|---|---|---|---|
| `Halt(scope)` | Stop new entries for scope (global, strategy, or instrument) | Operator | Yes |
| `Resume(scope)` | Begin reconciliation for scope; resume only if clean | Operator | Yes |
| `CancelAll(scope)` | Cancel open orders in scope | Operator | Yes |
| `Kill()` | Global halt, cancel all, flatten if configured | Operator + confirmation | Yes |
| `TightenLimit(id, value)` | Lower a limit's effective value; never raise | Operator | Yes |
| `SwapParams(strategy, versionHash)` | Activate a stored parameter version at next cycle | Operator | Yes |
| `GetStatus()` | Read-only state snapshot | Any authenticated client | No |

### 12.3 Authentication

- UDS peer credentials (`SO_PEERCRED`) must match the expected user.
- Commands carry a per-session token issued by `control_api` at login.
- The UI server binds to `127.0.0.1` only, uses a random token, and enforces CSRF tokens on state-changing requests.

## 13. Configuration and Profiles

### 13.1 Profiles

| Profile | Execution | Data | Credentials | Promotion file |
|---|---|---|---|---|
| `paper` | Mock adapter | Live market data | None required | No |
| `testnet` | Delta testnet | Delta testnet | Testnet keys | No |
| `live` | Live venue | Live | Live keys (secret store) | **Required** |

The engine refuses to start without an explicit profile. Each run writes the profile's content hash to the journal (`config_versions`).

### 13.2 Promotion File

Contains: release tag, engine build hash, config hash, paper-trading report hash, approver identity, approval timestamp, and an expiry. Signed with the operator's key. The engine verifies the signature and that the config hash matches the running profile. Without a valid file, live mode does not start.

## 14. Research Architecture

```
research/
  tradelab.engine  ──▶ pybind11 module ──▶ same strategy library as trader
  data ingest ──▶ quality checks ──▶ Parquet store ──▶ backtest runner
  backtest runner ──▶ event-driven engine (same risk gate in simulated mode)
  walk-forward ──▶ reports (config hash, data snapshot ID, engine build hash)
```

Backtests use the same risk gate code with simulated brokers. Results are only comparable when the config hash, data snapshot, and engine build hash match.

## 15. Security Architecture

| Asset | Protection |
|---|---|
| API keys, secrets, TOTP seeds | OS keyring or secret store; read at startup into locked memory; never logged |
| Journal | File permissions restricted to `trading`; optional encryption at rest |
| Database | Local, bound to localhost; separate roles for projector, control_api, and read-only reports |
| Control socket | Peer credentials and token |
| UI | Localhost only; token; CSRF |
| Source repository | No secrets; gitleaks in pre-commit and CI |
| Dependencies | Lockfile; vulnerability scanning in CI |

Log redaction is applied at the logger. Any value tagged as secret is rendered as `***`.

## 16. Deployment Topology

- Single Linux host (dedicated machine or VM), with chrony, isolated CPU cores, a separate disk for the journal, and an uninterruptible power supply recommended.
- systemd units for `trader`, `watchdog`, `projector`, `control_api`, and `ingestion`.
- PostgreSQL on the same host, bound to localhost.
- Development on laptops or a separate host, with the same build presets. Live profiles never run on development machines.

## 17. Failure Modes and Designed Responses

| Failure | Detection | Response | Recovery |
|---|---|---|---|
| Broker WebSocket drop | Heartbeat timeout | Feed DOWN; orders blocked; cancels attempted | Reconnect with backoff; reconcile; resume |
| Broker REST 5xx or timeout | Error class | Retry with backoff after state check | Automatic if resolved; otherwise UNKNOWN |
| Order outcome unknown | Timeout after send | UNKNOWN; instrument blocked | §5.4 |
| Sequence gap | Gap detection | RESYNCING; orders blocked | Snapshot; resume |
| Stale data | Threshold | STALE; RL-15 blocks | Automatic on fresh data |
| Reconciliation mismatch | Reconciler | Halt affected scope; AL-03 | Operator review and clear |
| Engine crash | Watchdog heartbeat loss | Watchdog cancels all, flattens if configured, AL-08 | systemd restart; journal recovery; reconciliation |
| Watchdog failure | `trader` cannot receive ack | `trader` halts new orders | Restart watchdog; exchange dead-man switch covers host loss |
| Host loss | Exchange-side dead-man switch | Exchange cancels resting orders | Restore host; reconcile |
| Journal disk full | Monitor | Halt new entries; AL-09 | Free space; never delete unarchived data |
| Torn journal tail | Recovery scan | Truncate; AL-14 | Continue after reconciliation |
| Clock skew | Clock-health monitor | Block time-based signals; AL-10 | Restore sync |
| Database unavailable | Projector | Buffer; AL-11 after 5 min | Projector catches up |
| Authentication failure | Adapter | Cancel-only; AL-12 | Operator re-authenticates |
| Exchange maintenance | Calendar | Cancel-only before window; halt during | Reconcile after window |

## 18. Architecture Decision Records (Index)

| ADR | Decision | Status |
|---|---|---|
| ADR-0001 | Journal is the source of truth; database is a projection | Accepted |
| ADR-0002 | Order intents are fsynced before sending (correctness over latency) | Accepted |
| ADR-0003 | Strategies are compiled statically; no runtime plugin loading | Accepted |
| ADR-0004 | Groww adapter behind `TRADING_ENABLE_GROWW`, OFF by default | Accepted |
| ADR-0005 | pybind11 binding of the strategy library for research parity | Accepted |
| ADR-0006 | gRPC on the control plane only; never on the data path | Accepted |
| ADR-0007 | Monorepo | Accepted |
| ADR-0008 | Delta testnet is the first live-capable venue | Accepted |
| ADR-0009 | Risk limits are `constexpr`; runtime may only tighten | Accepted |
| ADR-0010 | Watchdog is a separate process with separate credentials and a minimal link set | Accepted |
| ADR-0011 | Logging library selection | Proposed (see TECHNICAL_IMPLEMENTATION.md §7) |
| ADR-0012 | Numeric production risk limits | Pending (Phase 7 gate) |
| ADR-0013 | Partitioned projection unique keys include `event_time` | Accepted |

New ADRs go in `docs/adr/` using the template in PROCESS_PROJECT_MANAGEMENT.md §5.6. ADRs are never deleted; superseded ADRs are marked as such.
