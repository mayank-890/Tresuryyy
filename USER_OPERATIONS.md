# User and Operations Document

## Automated Derivatives & Commodities Trading System

| Field | Value |
|---|---|
| Document ID | UO-001 |
| Version | 0.1 |
| Audience | The operator (you). Read this before starting paper mode and keep it open during trading sessions. |
| Related | DESIGN_ARCHITECTURE.md (how it works), TESTING_QA.md §12 (pre-live checklist), `docs/runbooks/` (detailed procedures) |

---

## 1. What the System Does

The system watches market data for the instruments you have configured, runs strategies that decide when to buy or sell, checks every proposed order against hard risk limits, and sends approved orders to the broker. It records every step in a journal and shows its state on a local dashboard.

It is designed so that:
- it does not send an order it cannot record first,
- it stops opening positions when anything looks wrong,
- it never resolves an uncertain order by sending a new one,
- and it can be stopped from the dashboard, from the command line, and automatically if it goes silent.

It does not make you money by itself. Strategies are not guaranteed to be profitable, and the risk limits are set by you. Your decisions about limits, capital, and whether to go live carry the most weight.

## 2. Key Concepts

| Term | Meaning |
|---|---|
| **Profile** | A named configuration: `paper`, `testnet`, or `live`. The system will not start without one. |
| **Paper mode** | Live market data, simulated orders. No money at risk. |
| **Testnet** | Delta's test exchange. Real API, fake money. |
| **Live** | Real orders with real money. Requires a signed promotion file. |
| **Halt** | Stop opening new positions. Existing orders and positions remain. |
| **Cancel-only** | Cancel resting orders and reduce positions; open nothing new. |
| **Kill** | Halt, cancel everything, and flatten positions if configured. |
| **Reconciliation** | Comparing the system's view of orders and positions with the broker's. Must be clean before trading resumes. |
| **UNKNOWN order** | An order whose outcome the system could not confirm. Resolved automatically where possible; otherwise it needs your attention. |
| **Journal** | The system's own complete log. The database is a copy for reports. |

## 3. Setup (First Time)

### 3.1 Host Requirements

- Linux host (dedicated machine or VM) with stable power and internet. An uninterruptible power supply is strongly recommended for live.
- chrony running and synchronized. The system will refuse to trade if the clock is out of tolerance.
- A separate disk or partition for the journal.
- PostgreSQL 16 on localhost.
- Browser on the same host or a trusted local network segment, to open the dashboard.

### 3.2 Install

```bash
just setup              # toolchains, dependencies, database container, pre-commit hooks
just build              # build the default preset
just test               # run tests; expect all green before going further
```

If any step fails, stop and check `docs/runbooks/setup-failures.md`.

### 3.3 Secrets

Store these in the OS keyring or the host secret store. Do not put them in files in the repository, in profiles, in shell history, or in chat.

| Secret name | Used by | Needed for |
|---|---|---|
| `trading/delta/testnet/key` and `/secret` | `trader` | Testnet |
| `trading/delta/live/key` and `/secret` | `trader` | Live |
| `trading/watchdog/delta/key` and `/secret` | `watchdog` (narrow permissions) | Live |
| `trading/pg/projector` | `projector` | Reports |
| `trading/pg/control-api` | `control_api` | Dashboard |
| `trading/groww/totp-seed` | Groww adapter | Only if enabled (see §11) |

Create each key on the broker with the **narrowest permissions** the broker offers. Watchdog keys should permit cancel and position reduction only.

Command to store a secret:

```bash
trading-secrets set trading/delta/testnet/key      # prompts; does not echo or log
```

### 3.4 Profiles

Profiles live in `config/profiles/`. Do not edit `live.toml` casually. Changes to it are reviewed as risk changes (TECHNICAL_IMPLEMENTATION.md §9).

To check a profile without starting:

```bash
trading config validate --profile paper
```

## 4. Starting and Stopping

### 4.1 Paper Mode (Always Start Here)

```bash
trading start --profile paper
trading status
```

Open the dashboard at `http://127.0.0.1:8765` (address shown in `trading status`). The system should show:
- Engine state: `RUNNING`
- Feed state for each instrument: `HEALTHY`
- Reconciliation: `CLEAN`

### 4.2 Testnet

Same procedure with `--profile testnet`. Testnet secrets must be present.

### 4.3 Live (Gated)

Live starts only when all of these are true:
1. Gates G-LIVE-1 through G-LIVE-7 (PRD §12) are recorded.
2. The pre-live checklist (TESTING_QA.md §12) is signed.
3. A promotion file exists for the current release tag and config hash.
4. The first-session plan (§4.4) is reviewed.

```bash
trading config validate --profile live
trading start --profile live
```

If the promotion file is missing, invalid, expired, or does not match, the engine refuses to start and writes the reason to the log and the dashboard.

### 4.4 First Live Session Plan

- One instrument. Smallest lot size. Capital at the minimum approved value.
- Stay at the dashboard for the whole session. Do not leave it unattended for the first 3 sessions.
- Before the open: confirm `trading status` shows `RECONCILED` for all scopes.
- Watch the first order end to end: intent journaled → sent → acked → filled (or cancelled). Confirm the dashboard shows each state with its sequence number.
- Run a manual halt once, then resume after reconciliation, to verify the path works with real orders.
- At close: confirm the positions on the dashboard match the broker's app. Check the daily report.
- Do not increase limits until the review in §7.6 is done.

### 4.5 Stopping

```bash
trading stop                       # graceful: halts entries, cancels resting orders in scope, shuts down
trading stop --keep-orders         # shuts down, leaves resting orders for the exchange to hold
```

Use `--keep-orders` only when you understand the consequence: orders stay live at the broker while the engine is down. The exchange dead-man switch (if configured) may cancel them.

Stopping does not change limits or delete the journal.

## 5. The Dashboard

### 5.1 Panels

| Panel | What to look for |
|---|---|
| **System** | Engine state; uptime; profile name; build version; config hash (first 8 characters) |
| **Kill and controls** | Global halt, kill switch, per-strategy halt, resume, cancel-all |
| **PnL** | Realized, unrealized, and day-to-date, per strategy and total. Compare against the daily loss limit line. |
| **Positions** | Net quantity and average price per instrument; broker-reported value and a match indicator |
| **Orders** | Open orders with state, client ID, sequence number, age. UNKNOWN orders are highlighted. |
| **Feeds** | Per-instrument health and last update age |
| **Margin** | Available margin, post-trade headroom, margin data age |
| **Risk events** | Every rejection and limit breach, with limit ID and values |
| **Reconciliation** | Last run time, result per scope, any open mismatch |
| **Alerts** | Active and recent alerts (§8) |

### 5.2 State Indicators

| Indicator | Meaning | Operator action |
|---|---|---|
| **RUNNING** (green) | Normal trading | None |
| **DEGRADED** (amber) | Connection or feed problem; new entries blocked | Watch. If not resolved in 2 minutes, see runbook RB-03 |
| **CANCEL_ONLY** (amber) | Only cancels and reductions permitted | Check the reason in Alerts |
| **HALTED** (red) | No new orders; reconciliation may be needed | Find the reason; resume only after clean reconciliation |
| **RECONCILING** (blue) | Comparing state with the broker | Wait; do not change anything |
| **STARTING** (blue) | Recovering from the journal | Wait; do not restart |
| **SHUTDOWN** (grey) | Stopped | — |
| Feed **HEALTHY** | Data is current | None |
| Feed **STALE** | No recent updates | Check market hours and network |
| Feed **RESYNCING** | Rebuilding data after a gap | Wait; usually under 30 seconds |
| Feed **DOWN** | Connection lost | See RB-03 |

### 5.3 Reading an Order

Each order shows its client ID (for example `S7F2-0000012345`), the strategy that created it, its state history with sequence numbers, and the risk decision that approved it. The client ID is the reference you give to support or write in an incident record.

## 6. Controls and How to Use Them

### 6.1 Which Control to Use

| Situation | Use | Effect |
|---|---|---|
| Unusual behavior from one strategy | Halt strategy | That strategy stops opening positions; others continue |
| Market conditions you don't trust | Global halt | No new positions anywhere; orders and positions stay |
| Need to reduce exposure without new risk | Cancel-only | Only cancels and reductions allowed |
| Something is seriously wrong | Kill | Halt, cancel all, flatten if configured |
| Want to stop the engine | `trading stop` | Graceful shutdown |
| Want to adjust a limit | Tighten limit (dashboard or CLI) | Lowers the limit immediately; logged |

### 6.2 Kill Switch

There are three independent kill paths. Each one works if the others fail.

| Path | Triggered by | Works when | Test frequency |
|---|---|---|---|
| **1. Software kill** | Dashboard button or `trading kill` | Engine and control socket are running | Weekly drill |
| **2. Watchdog** | Automatic if the engine stops sending heartbeats | Watchdog is running; the engine has stopped | Weekly drill (simulated) |
| **3. Exchange-side** | Exchange dead-man switch expires | The host is unreachable; exchange supports the feature | Monthly drill on testnet |

**Dashboard kill:** click **Kill**, read the confirmation text, type `KILL` to confirm. The system records your identity and the time. The kill button uses the same risk gate and the same control path as every other command; it is not a separate trading path.

**CLI kill:** `trading kill --scope global --confirm`

**Watchdog kill:** operates without you. You will see its activation in the alerts and journal. After activation, follow runbook RB-05.

**Exchange-side kill:** if the host is lost, the exchange should cancel resting orders after the dead-man switch expires. Positions are not flattened by this path. Check the broker app.

### 6.3 Halt and Resume

- **Halt** takes effect immediately on the next decision cycle.
- **Resume** does not restart trading immediately. The system reconciles first. Resume completes only when reconciliation is clean for that scope.
- If reconciliation shows a mismatch, the system stays halted. You must review the mismatch (dashboard → Reconciliation → Details) and choose one of:
  - **Accept broker state** — the system adopts what the broker shows, with a recorded reason
  - **Hold** — stay halted and investigate
  - **Flatten** — close the mismatched position through the broker's app, then re-reconcile

Never choose "accept broker state" without checking the broker's app yourself first.

### 6.4 Tightening a Limit

Dashboard → Limits → select limit → enter a lower value → confirm. The new value takes effect on the next evaluation and is logged.

You cannot raise a limit from the dashboard. Raising a limit is a code and review process (TECHNICAL_IMPLEMENTATION.md §9).

## 7. Routine Operation

### 7.1 Daily Schedule (Live or Paper)

| Time (IST) | Activity |
|---|---|
| Before 8:30 | Check host is up, chrony synchronized (`trading status` shows clock OK); check previous day's report; check alerts |
| Before market open | Confirm profile, engine state, feed health, reconciliation clean, margin data fresh |
| Market open | Watch first 15 minutes at the dashboard: orders, fills, feed health |
| Intraday | Check dashboard every 30 minutes at minimum; respond to alerts within 5 minutes during session |
| Before market close | Review open orders and positions; decide whether to halt new entries near expiry or close |
| After close | Wait for final reconciliation; review the daily report; confirm positions against broker app |
| End of day | Record any incidents; update notes; confirm backups completed |

Exchange sessions differ by product. Commodity sessions run on different hours from equity derivatives. The calendar in `config/calendars/` is authoritative; check it when a new product is added.

### 7.2 Weekly

- Kill-switch drill (software and watchdog; exchange-side monthly). Procedure: RB-06.
- Review risk events log: every rejection, grouped by limit ID. A limit that rejects often may need a strategy change, not a higher limit.
- Review alerts for patterns.
- Check disk space on the journal volume.
- Confirm backups restored successfully on a test database (monthly, with the weekly review).

### 7.3 Monthly

- Rotate API keys (§9.2).
- Exchange-side kill drill on testnet.
- Review compliance records for changes in broker terms or regulations (PRD §12).
- Review the operator's risk parameters against recent performance.
- Archive journal segments older than the retention window, after archive verification.

### 7.4 End of Phase

Each phase end has a review note. See PROCESS_PROJECT_MANAGEMENT.md §6.3.

### 7.5 Reading the Daily Report

The daily report (generated after final reconciliation) contains:

| Section | Contents | What to check |
|---|---|---|
| Summary | PnL by strategy; number of orders; fill rate; rejections | PnL within expected range? Rejections explained? |
| Orders | Every order with client ID, decision context, config hash | Any UNKNOWN? Any duplicate-looking order? |
| Fill quality | Realized slippage vs. model, per instrument | Materially worse than the model? Investigate |
| Risk events | Every rejection and limit breach | Any limit that was hit but should not have been? |
| Reconciliation | Results and any resolved mismatches | Any mismatch resolved without explanation? |
| Alerts | Summary of alerts and actions taken | Every alert has an action recorded |
| System | Uptime, restarts, latency summary, journal size | Any unexpected restart? |

### 7.6 Before Increasing Limits or Size

Do not increase size or limits until:
- at least 10 consecutive sessions have ended with clean reconciliation and no S0/S1 incidents,
- the fill-quality section shows slippage within the model's tolerance,
- the risk events log has no unexplained rejections,
- and you have written an ADR for the new values (ADR-0012 or a successor).

## 8. Alerts

### 8.1 Severity Levels

| Level | Meaning | Response time |
|---|---|---|
| **Critical** | Money at risk or safety control failed | Immediately |
| **High** | Trading affected; intervention likely | Within 5 minutes during session |
| **Medium** | Degraded function; trading may continue | Within 30 minutes |
| **Info** | Recorded for review | At daily review |

### 8.2 Catalog

Thresholds shown are defaults; each is configurable in the profile and is set during Phase 3.

| ID | Alert | Severity | Trigger | Expected action |
|---|---|---|---|---|
| AL-01 | Feed stale | High (in session) | No update for an instrument beyond threshold | Check network and market hours; RB-03 if persistent |
| AL-02 | Broker disconnected | High | Private or public feed DOWN beyond 10 s | Monitor; RB-03 |
| AL-03 | Reconciliation mismatch | Critical | Broker state differs from journal | Halt starts automatically; RB-02 |
| AL-04 | Order UNKNOWN > 30 s | High | Order not resolved in 30 s | RB-04 |
| AL-05 | Rate budget > 80% | Medium | Sustained high request usage | Reduce activity; check strategy behavior |
| AL-06 | Margin below buffer | High | Post-trade margin headroom below buffer | Entries blocked automatically; review positions |
| AL-07 | Daily loss ≥ 60% of limit | High | Approaching RL-10 | Review strategy; consider halt |
| AL-08 | Watchdog heartbeat missed | Critical | Engine heartbeat lost | Watchdog acts; RB-05 |
| AL-09 | Journal disk > 80% full | High | Storage threshold | Free space via archive (RB-07); never delete unarchived journal |
| AL-10 | Clock skew beyond tolerance | High | Local time differs from synchronized source | Time-based signals blocked; fix chrony (RB-08) |
| AL-11 | Projector lag > 5 min | Medium | Database behind journal | Check PostgreSQL; trading unaffected |
| AL-12 | Authentication failure | Critical | Broker rejects credentials | Cancel-only; re-authenticate (RB-09) |
| AL-13 | Kill switch activated | Critical | Any kill path | Read RB-05; confirm state |
| AL-14 | Torn journal tail recovered | High | Recovery truncated an incomplete record | Verify the last events against broker; RB-10 |
| AL-15 | Limit tightened | Info | Any limit change | Confirm it was intended |
| AL-16 | Live started / promotion verified | Info | Engine start in live | Confirm release and config hash |
| AL-17 | Engine restarted | High | systemd restart | Check journal recovery; RB-11 |
| AL-18 | Strategy halted by risk | Medium | RL-09, RL-10, or RL-11 breach for a strategy | Review; decide resume or leave halted |

### 8.3 Delivery

Alerts appear on the dashboard and in the log. For critical and high alerts during live trading, they are also sent to the channel configured in `config/profiles/live.toml` (for example, a messaging app or a phone notification). Alerts must not be the only safety mechanism. The system's safety does not depend on you reading an alert in time.

Test alert delivery at the start of each week (RB-06).

## 9. Maintenance

### 9.1 Backups

| Item | Frequency | Retention | Verification |
|---|---|---|---|
| Journal segments | Continuous (on the journal volume) | Per retention policy; archived before removal | Monthly restore test on a copy |
| PostgreSQL | Daily full, hourly WAL | 30 days | Monthly restore to a test instance |
| Profiles and config | On change (git) | Full history in git | — |
| ADRs and runbooks | On change (git) | Full history | — |
| Secrets | Not backed up by the system. Keep a documented recovery procedure in your password manager or the host's secret store backup. | — | Annual |

### 9.2 Key Rotation

- Rotate live keys monthly and after any suspected exposure.
- Procedure: create new keys with the same narrow permissions → update the secret store → restart the engine in halted state → reconcile → resume → revoke old keys.
- Never rotate keys during an open session with orders resting unless you have checked the reconciliation after the restart.

### 9.3 Software Updates

- Updates are tagged releases only (PROCESS §9).
- Update procedure: halt → cancel-only → wait for resting orders to close or be cancelled → stop → install release → validate config → start in paper → verify → start live (only if live promotion file matches the new tag).
- Never update during a session with open positions unless it is a security fix you have reviewed.

### 9.4 Journal Archive and Retention

- Segments are archived after the retention window (default 90 days; set in profile).
- Archive verification: checksums match, and a sample of records is readable by the current inspector.
- Only verified archives allow removal from the live volume.

## 10. Incident Response

### 10.1 Severity

| Severity | Examples | Response |
|---|---|---|
| S0 | Unintended order, unexplained position, risk limit bypassed, secret exposed | Kill immediately; then investigate |
| S1 | Reconciliation mismatch, UNKNOWN order not resolved, watchdog activated, repeated disconnects during session | Halt; investigate before resume |
| S2 | Feed issues without trading impact, dashboard fault, projector lag | Note; fix within the phase |
| S3 | Cosmetic | Note |

### 10.2 Steps for Any Incident

1. **Stabilize.** If trading is at risk, halt or kill first. Investigate afterward.
2. **Check the broker app directly** for orders and positions. The broker's view is the reference for money.
3. **Capture evidence.** `trading diagnose --since <time>` produces a bundle: journal extract, logs, alerts, configuration hash. Store it under `docs/incidents/` with the incident record.
4. **Determine the runbook** (§11) and follow it.
5. **Record** the incident using the template below within 24 hours.
6. **Decide** whether a code change, a configuration change, or an operating change is needed. Open a ticket for the change.

### 10.3 Incident Record Template

```markdown
# Incident YYYY-MM-DD-<slug>

- Severity: S0 | S1 | S2 | S3
- Detected: <time, how>
- Duration: <start – end>
- Trading impact: <orders affected, positions, PnL effect>
- Client IDs involved: <list>
- Alerts raised: <IDs>
- Actions taken: <ordered list with times>
- Root cause: <known | suspected | unknown — and what would confirm it>
- Broker app verified: <yes/no, what was checked>
- Follow-up tickets: <IDs>
- Runbook used: <RB-##>
```

## 11. Runbooks (Summary)

Full procedures are in `docs/runbooks/`. Summaries here show when to use each one.

| ID | Runbook | Use when |
|---|---|---|
| RB-01 | First-time setup failures | `just setup` or first start fails |
| RB-02 | Reconciliation mismatch | AL-03 |
| RB-03 | Broker or feed disconnect | AL-01, AL-02 persist beyond 2 minutes |
| RB-04 | UNKNOWN order | AL-04 |
| RB-05 | Kill switch activated | AL-08, AL-13 |
| RB-06 | Weekly drill procedure | Every week |
| RB-07 | Journal storage | AL-09 |
| RB-08 | Clock skew | AL-10 |
| RB-09 | Authentication failure | AL-12 |
| RB-10 | Torn journal tail | AL-14 |
| RB-11 | Unexpected engine restart | AL-17 |
| RB-12 | Database unavailable | AL-11 |
| RB-13 | Host failure or power loss | Host unreachable |
| RB-14 | Exchange maintenance | Broker announces maintenance |
| RB-15 | Suspected double position | Broker shows more position than journal, or two orders with the same intent |

### 11.1 RB-02 Summary: Reconciliation Mismatch

1. Keep halted. Do not resume.
2. Open Reconciliation → Details. Note the scope, the expected value, and the broker value.
3. Check the broker app for the same scope. Compare order list and fills.
4. Identify the cause: missed fill event (common after disconnect), manual action taken in broker app, or a system defect.
5. If the broker is right and the journal is missing an event: resolve with "Accept broker state" and reason. Record the incident.
6. If there is a manual action you didn't intend: flatten through the broker app as needed, then re-reconcile.
7. If the cause is a system defect: keep halted; create an S1 ticket; do not resume until fixed or until a risk-acceptable workaround is approved.

### 11.2 RB-04 Summary: UNKNOWN Order

1. Do not place a replacement order manually for the same intent.
2. Check the broker app for an order with the client ID.
3. If found: the system will resolve it automatically within the retry window. Confirm on the dashboard.
4. If not found after the system's query window: the system will resend once with the same ID. Watch the outcome.
5. If still unresolved after that: the instrument stays blocked. Decide whether to leave it blocked (safe default) or cancel related orders via the broker app. Record the decision.

### 11.3 RB-05 Summary: Kill Switch Activated

1. Confirm the engine state on the dashboard, or `trading status`.
2. Check the broker app: open orders should be zero (or only orders you deliberately kept), positions as expected.
3. If the watchdog acted, check whether the engine is running. If it is not, restart it in paper mode first if you are unsure of the state.
4. Find the reason in the alerts and the journal. Do not resume until the cause is understood.
5. Resume only through the normal resume path with reconciliation.

### 11.4 RB-15 Summary: Suspected Double Position

1. Halt everything. Do not place orders.
2. Compare the broker app's position with the dashboard for each affected instrument.
3. Find the client IDs involved. The journal shows whether one or two order intents were created.
4. If the system sent two orders for one intent, that is an S0 defect. Keep the system halted, collect the diagnose bundle, and open a P0 ticket.
5. Flatten the excess only after confirming the exact amount in the broker app.

## 12. Configuration Changes

| Change | How | Who approves | Effect |
|---|---|---|---|
| Strategy parameters | Create new parameter version; `SwapParams` at a decision boundary | Operator | Next decision cycle |
| Tighten a limit | Dashboard or CLI | Operator (logged) | Immediate |
| Loosen a limit | Code and ADR; restart; paper run first | Operator with independent review | After release |
| Add instrument | Edit instrument master; validate; restart in paper | Operator | After restart |
| Change stale threshold | Profile edit; validate; restart | Operator | After restart |
| Change live profile | Class A change; new promotion file | Operator | After new release |

## 13. Working Within the Limits of the System

- **The system does not know your intent.** It executes the strategies you approved within the limits you set.
- **Risk limits are only as good as their values.** An unreasonably high limit will allow losses that you consider unacceptable. Set them from the capital you can afford to lose.
- **The dashboard is not a guarantee of safety.** It reflects the system's view. The broker app is the reference for money.
- **Paper results overstate live results.** Assume fills and slippage are worse than the paper model until live data says otherwise.
- **Automated systems fail in novel ways.** When something looks unfamiliar, halt and investigate. Halting costs little; continuing to trade through a problem can cost a lot.
- **Taxes, fees, and regulations change.** Confirm current rules with an advisor; the system does not handle tax reporting.

## 14. Troubleshooting Quick Reference

| Symptom | Likely cause | First check | Go to |
|---|---|---|---|
| Engine won't start: "promotion" error | Live profile without valid promotion file | Promotion file, tag, config hash | §4.3 |
| Engine won't start: clock error | chrony not synchronized | `chronyc tracking` | RB-08 |
| Engine won't start: journal error | Torn tail or CRC failure | Recovery log | RB-10 |
| Feed STALE all day | Market closed or wrong calendar | Calendar; time zone | §7.1 |
| Dashboard does not load | Control API down or port changed | `trading status` | RB-11 |
| Dashboard shows numbers that differ from the broker | Projector lag or reconciliation pending | Projector lag; reconciliation | AL-11, RB-02 |
| Orders rejected often | Margin, price collar, or limit | Risk events grouped by limit ID | §7.2 |
| Strategy seems idle | Halted, stale data, or no signal | Strategy state; feed state | §5.2 |
| Alert delivery not received | Notification channel misconfigured | Test alert | RB-06 |

## 15. Things You Must Never Do

1. Start live without a valid promotion file and all gates recorded.
2. Place a manual order for an intent the system has marked UNKNOWN, without checking the broker app first.
3. Resume after a mismatch without reviewing it.
4. Share API keys, TOTP seeds, or account identifiers in chat, screenshots, issue trackers, or commits.
5. Edit the journal files. Use the inspector (read-only) to view them.
6. Delete journal segments that have not been archived and verified.
7. Restart the engine repeatedly during an open session. Each restart is a recovery; repeated restarts hide the underlying problem.
8. Raise a limit from the dashboard. It is not possible by design, and attempts to work around it are a security concern.
9. Run two engines for one profile. The system prevents it; do not try to bypass the lock.
10. Trust a single alert channel. Check the dashboard as well.

## 16. Glossary of Dashboard Terms

| Term | Definition |
|---|---|
| Client order ID | The system's unique identifier for an order; the broker echoes it when supported |
| Sequence number | Position of a record in the journal; increases with every event |
| Config hash | Fingerprint of the profile in use; identifies exact configuration |
| Approval token | Permission from the risk gate for one order; expires after 500 ms |
| Collar | Maximum allowed distance of an order price from the reference price |
| Reduce-only | Mode in which only orders that reduce exposure are allowed |
| Dead-man switch | Exchange-side timer that cancels orders if the system stops refreshing it |
| Watchdog | Separate process that acts if the engine stops responding |
| Reconciled | System state matches broker state for the stated scope |
