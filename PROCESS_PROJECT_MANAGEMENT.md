# Process and Project Management Document

## Automated Derivatives & Commodities Trading System

| Field | Value |
|---|---|
| Document ID | PM-001 |
| Version | 0.1 |
| Related | PRD.md (scope and gates), TESTING_QA.md (quality gates), USER_OPERATIONS.md (run procedures), AGENTS.md and AGENT_PROMPTS.md (agent operation) |

---

## 1. Operating Model

The project has one human operator and AI implementation agents.

| Role | Who | Responsibilities |
|---|---|---|
| Owner and operator | Human | Sets priorities; defines tickets; reviews all Class A changes; owns risk parameters; approves ADRs; authorizes every live gate |
| Primary implementation agent | Codex | Implements tickets; writes tests; keeps PROGRESS.md current |
| Fallback implementation agent | Cursor | Resumes Codex work when Codex is unavailable; follows the same rules |
| Reviewer | Human (plus an independent review agent for high-stakes changes, §5.5) | Reviews diffs; verifies tests; signs off on Class A changes |

**Principle: agents do the typing; the human owns the decisions.** Agents may propose designs and open questions. They do not decide risk parameters, enable live features, or merge Class A changes without human review.

### 1.1 Tool Priority

| Priority | Tool | Use when |
|---|---|---|
| 1 | Codex | Default for all implementation tickets |
| 2 | Cursor | Codex usage limit reached, or Codex is unavailable |
| — | Human | Design decisions, reviews, risk and compliance decisions, gate approvals |

Switching tools is a handoff. The protocol is in §7. Both tools read the same repository rules (`AGENTS.md`), so no tool-specific rules exist that the other lacks.

## 2. Work Management

### 2.1 Source of Truth for Work

The repository is the source of truth for work that agents perform:
- `PROGRESS.md` holds the current phase, the backlog for the current phase, the active ticket, open questions, and the handoff block.
- Ticket specifications live in `docs/tickets/T-####.md` for any ticket that needs more than its PROGRESS.md entry.
- An external issue tracker may be used for the operator's own planning. Agents do not depend on it; its content must be copied into PROGRESS.md for any work an agent will do.

### 2.2 Ticket Lifecycle

```
Backlog → Ready → In progress → In review → Done
            │          │             │
            │          └─ Blocked ───┘  (blocker recorded in PROGRESS.md)
            └─ Rejected (recorded with reason)
```

| State | Entry criteria |
|---|---|
| Backlog | Described; may lack acceptance criteria |
| Ready | Has ID, phase, FR references, acceptance criteria, files-allowed list, test requirements, risk class |
| In progress | Branch created; active in PROGRESS.md |
| In review | PR open; CI running or passing |
| Done | Merged; acceptance criteria verified; PROGRESS.md updated |
| Blocked | Dependency or open question prevents progress; reason recorded |

Only one ticket is in progress per agent session. Agents do not start a second ticket until the first is Done or Blocked.

### 2.3 Estimation

Tickets are sized S (under half a day of agent work), M (up to a day), or L (must be split before starting). No velocity commitments are made. Phase timing comes from the roadmap, and it moves when phase exit criteria are not met.

## 3. Risk Classification of Changes

Every ticket and PR is classified. The class determines the review and test requirements.

| Class | Paths | Review | Merge |
|---|---|---|---|
| **A: Safety-critical** | `engine/src/risk/`, `engine/src/orders/`, `engine/src/journal/`, `engine/src/broker/`, `engine/src/control/`, `engine/apps/watchdog/`, `db/migrations/`, `schemas/journal/`, `config/profiles/live.toml`, `deploy/`, `proto/control/` | Human review required; no agent auto-merge; replay parity and chaos tests where relevant | Human merges |
| **B: Trading and presentation** | `engine/src/strategy/`, `engine/src/marketdata/`, `engine/src/portfolio/`, `research/`, `services/`, `ui/`, `config/` (non-live) | Human review required for first PR of each module; standard review thereafter | Human merges |
| **C: Tooling and documentation** | `docs/`, `tools/`, `tests/fixtures/`, CI configuration (not safety gates), formatting | Standard review | Human merges |

**Rule:** a PR touching any Class A path is Class A, regardless of its size or the ticket's label.

**Rule:** CI configuration that affects safety gates (sanitizers, replay parity, dependency rules, secret scanning) is Class A.

## 4. Git Workflow

### 4.1 Branches

- `main` is protected. Direct pushes are disabled. Every change arrives by pull request.
- Branch names: `feat/T-####-short-slug`, `fix/T-####-short-slug`, `docs/T-####-short-slug`, `chore/T-####-short-slug`.
- Agents work only on their ticket branch.
- Wip branches for handoff: `wip/T-####-<tool>-<date>` (see §7.4).

### 4.2 Commits

- Small, green commits. Every commit builds and passes its own tests.
- Message format: `T-####: <imperative summary>`. Body explains why when not obvious.
- Example: `T-0005: add SPSC ring with release/acquire ordering and wraparound tests`.
- No commits containing secrets, generated binaries, large data files, or unformatted code.

### 4.3 Pull Requests

Each PR contains:
- Ticket ID and link to the PROGRESS.md entry or ticket file
- Risk class
- Requirement IDs addressed (for example, FR-RK-01, RL-03)
- Test evidence: which tests were added or changed and the commands run
- Replay parity result when Class A or strategy code is touched
- Any ADR created or referenced
- Known limitations and open questions
- Checklist (below)

**PR checklist**
- [ ] `just check` passed locally on the commit
- [ ] No changes outside the ticket's files-allowed list, or the list was updated with justification
- [ ] No new dependency, or an ADR is linked
- [ ] No hand-edited generated file
- [ ] No disabled, skipped, or weakened test
- [ ] No secret, credential, or account identifier in the diff
- [ ] Logging does not expose secrets
- [ ] Documentation updated where behavior changed
- [ ] PROGRESS.md updated

### 4.4 Merge

- Squash merge for feature branches; the squashed message keeps the ticket ID.
- Required status checks from TESTING_QA.md §10 must pass.
- Class A requires the human reviewer's approval recorded on the PR.

## 5. Documentation and Decision Process

### 5.1 Document Set

| Document | Owner | Update trigger |
|---|---|---|
| PRD.md | Operator | Requirement or gate change |
| DESIGN_ARCHITECTURE.md | Operator, with agent drafts | Component, contract, or state-machine change |
| TECHNICAL_IMPLEMENTATION.md | Shared | Stack, standards, dependency, or schema change |
| PROCESS_PROJECT_MANAGEMENT.md | Operator | Process change |
| TESTING_QA.md | Shared | New test type, tolerance, or gate |
| USER_OPERATIONS.md | Operator, with agent drafts | Operational change, new alert, new runbook |
| AGENTS.md | Operator | Any change to agent rules |
| PROGRESS.md | Agents and operator | Every session |
| `docs/adr/` | Whoever proposes; operator accepts | Significant decision |
| `docs/runbooks/` | Operator | Incident or new failure mode |
| `docs/compliance/` | Operator | Regulatory or broker-term change |

### 5.2 Ticket Template

```markdown
# T-####: <Outcome in one line>

- Phase: <0–8>
- Risk class: <A | B | C>
- Requirements: <FR-XX-##, NFR-XX-##, RL-##>
- Depends on: <T-####, or none>

## Goal
<One paragraph: what is true when this is done.>

## Acceptance criteria
- [ ] <Observable, testable criterion>
- [ ] <…>

## Files allowed
- <path or glob>

## Out of scope
- <explicitly excluded work>

## Tests required
- <unit / integration / fuzz / TSan / replay / UI — and what each covers>

## Notes
- <Constraints from DESIGN or TECHNICAL docs that apply>
- <Open questions, with TODO(operator) where a value is needed>
```

### 5.3 Definition of Ready

A ticket can be started when: it has an ID, phase, requirement references, risk class, acceptance criteria, files-allowed list, test requirements, and no unresolved open question that blocks it.

### 5.4 Definition of Done

A ticket is done when all of the following are true:
1. Every acceptance criterion is met and each is linked to a passing test or an observed artifact.
2. Required tests exist, pass in the required presets, and were not weakened.
3. Lint, format, type checks, and the codegen drift check pass.
4. Documentation reflects the new behavior.
5. No new TODO without an owner tag.
6. PR merged after the required review.
7. PROGRESS.md shows the ticket as Done with the merge commit.
8. For Class A: a replay or chaos result is attached where the requirement calls for one.

### 5.5 Independent Review for Class A

For any Class A PR, a second review pass is performed by a separate agent session that has not seen the implementation conversation. It receives only the diff, the relevant requirement and design sections, and the test results. Its job is to find defects, missing tests, and mismatches with the spec. Its findings go into the PR. The human reviewer decides.

The reviewing agent must not have implementation rights in that PR, and its output is advisory.

### 5.6 ADR Template

```markdown
# ADR-####: <Title>

- Status: Proposed | Accepted | Superseded by ADR-#### | Rejected
- Date: YYYY-MM-DD
- Deciders: <operator>

## Context
<Forces, constraints, and the problem.>

## Decision
<What we will do.>

## Consequences
- Positive:
- Negative:
- Follow-up tickets:

## Alternatives considered
- <Option> — <why not>
```

ADRs are never deleted. Superseded ADRs stay in place with a pointer to their replacement.

## 6. Phase Plan and Exit Criteria

Each phase begins only when the previous phase's exit criteria are met. If a criterion fails, the phase is not complete; the schedule slips.

### 6.1 Phase Table

| Phase | Weeks | Primary deliverables | Exit criteria | Gate owner |
|---|---|---|---|---|
| 0 Foundation | 1–2 | Repository, CI, CMake superbuild, journal schema and codegen, core primitives, DB migrations, profiles, journal inspector | Clean clone builds and tests pass; migrations apply; secrets scan clean; sanitizers clean; engine starts in paper mode with no trading; journal inspector reads fixtures and detects torn tails | Operator |
| 1 Data infrastructure | 3–4 | Live tick capture to journal; historical ingestion; data quality checks | ≥ 2 weeks of continuous capture on the chosen venue; gap and staleness rates reported; journal recovery tested after forced kill | Operator |
| 2 Strategy library and backtester | 5–8 | Strategy interface; event-driven backtester; fill model; pybind11 module; walk-forward | Backtest matches independent Python reference within tolerance; walk-forward runs reproduce from config hash | Operator |
| 3 Live engine core | 9–12 | All engine threads; risk gate; order state machine; reconciler; watchdog; replay mode | Replay produces identical order intents to backtest on fixtures; every RL limit has boundary tests; no sanitizer findings; TSan clean; p99 latency measured and within NFR-PERF-01 or ADR filed | Operator |
| 4 Broker adapters | 13–15 | Delta adapter (testnet), mock exchange, Groww adapter against mock (build-flagged OFF) | All failure-injection tests pass; ambiguous-order recovery never creates a duplicate position; Delta testnet soak ≥ 3 days | Operator |
| 5 Chaos and failure testing | 16–17 | Chaos harness; three kill paths verified | Every scenario in TESTING_QA.md §6 reaches its documented safe state; watchdog activation latency within target | Operator |
| 6 Paper trading | 18–22 (min 4 weeks) | Paper runs on live data; daily comparison; drills | Paper vs. model divergence within TESTING_QA.md §8 tolerances; zero unexplained reconciliation mismatches; weekly kill drills passed; all PRD §12 gates G-LIVE-1 to G-LIVE-6 decided | Operator |
| 7 Limited live | 23 onward | One instrument, minimum size, tight limits | Gate G-LIVE-7 signed; daily reconciliation clean; zero S0/S1 incidents; live fills within calibrated model | Operator |
| 8 Operations | Continuous from 5 | Dashboard, alerts, daily reports, runbooks | All alerts in USER_OPERATIONS.md §8 fire in drills; daily report generated for 10 consecutive sessions | Operator |

### 6.2 Phase 0 Backlog (Initial)

These tickets are the first work for agents. They match the build order in the repository structure. The full text of each is in PROGRESS.md.

| ID | Title | Class | Depends on |
|---|---|---|---|
| T-0001 | Repository skeleton, .gitignore, editorconfig, README, AGENTS.md, docs set | C | — |
| T-0002 | Pre-commit hooks, gitleaks config, CI skeleton building nothing yet | C | T-0001 |
| T-0003 | CMake superbuild with presets; vcpkg manifest; empty targets; CTest wired | B | T-0002 |
| T-0004 | Journal schema v1 definitions and codegen for C++ and Python; drift check | A | T-0003 |
| T-0005 | Core primitives: clock, SPSC ring, object pool, log queue, thread pinning helper; TSan tests | A | T-0004 |
| T-0006 | Initial PostgreSQL migrations, roles, partition scripts; CI apply and down check | A | T-0002 |
| T-0007 | Profile schema, validator, promotion-file verifier (live refuses without it); paper mode starts with no trading | A | T-0003 |
| T-0008 | Journal inspector: read segments, validate CRC and sequence, detect and report torn tail; fixtures | C | T-0004, T-0005 |
| T-0009 | Python research package skeleton: uv, ruff, mypy, pytest, `tradelab.engine` stub | B | T-0002 |
| T-0010 | Frontend skeleton: static index, module structure, Playwright smoke test | B | T-0002 |
| T-0011 | Calendar and instrument master loader (initial files, validation) | B | T-0007 |

### 6.3 Milestone Reviews

At the end of each phase, the operator writes a review note in `docs/reviews/phase-N.md` with: exit-criteria status, deviations, new risks, open questions, and the decision to proceed, repeat, or re-plan.

## 7. Agent Operating Protocol

This section governs Codex and Cursor. AGENTS.md carries the operational rules; this section explains the reasons and the process.

### 7.1 Session Start

Every session begins with:
1. Read AGENTS.md.
2. Read PROGRESS.md, especially the handoff block and the active ticket.
3. Run `git status`, `git branch --show-current`, and `git log --oneline -5`.
4. If there are uncommitted changes, follow §7.5 before doing anything else.
5. Confirm the active ticket's files-allowed list and acceptance criteria.
6. Run `just check` to confirm the baseline is green. If it is not, the first task is to make it green or record why not.

### 7.2 During a Session

- Work in small steps. After each step that builds and passes its tests, commit and update PROGRESS.md.
- Checkpoint at least every 30 minutes of work or after every green test step, whichever comes first.
- Stop and record a question in PROGRESS.md, rather than guessing, when a decision involves risk, money, secrets, compliance, or a value marked TODO(operator).
- Do not expand scope. New work discovered during a ticket becomes a new backlog entry in PROGRESS.md.

### 7.3 Session End

- Commit the current green state.
- Update PROGRESS.md: ticket status, what was done, what is next, open questions, and the handoff block (§7.4).
- If the ticket is not finished, leave it In progress with a precise next step.

### 7.4 Handoff Block

The handoff block is the single place that lets either tool resume. It is updated at every checkpoint, not only at session end, because a usage limit can end a session at any time.

Format (in PROGRESS.md):

```markdown
## Handoff
- Updated: <ISO timestamp> by <codex | cursor | human>
- Active ticket: T-####
- Branch: feat/T-####-slug
- Last green commit: <sha> "<message>"
- Working tree: <clean | dirty — see note>
- Tests passing at last commit: <commands run>
- Next step (exact): <one concrete action>
- Files touched this ticket: <list>
- Decisions made this session (not yet in ADR): <list or none>
- Open questions: <list or none>
- Notes for the next tool: <anything that would otherwise be rediscovered>
```

### 7.5 Recovering From an Interrupted Session

When a session ends mid-change (for example, by a usage limit), the next session treats the uncommitted diff as unverified:
1. Save the diff: `git diff > .handoff/T-####-<timestamp>.patch` (`.handoff/` is git-ignored).
2. Run `just check`.
3. If the checks pass and the change matches the ticket, commit it with a message beginning `T-####: (recovered)`.
4. If the checks fail, fix only what is needed to make them pass. If the cause is unclear, record it in PROGRESS.md and continue from the last green commit, leaving the patch file in place.
5. Never discard a patch file without the operator's review.

### 7.6 Switching Tools (Codex to Cursor and Back)

| Step | Owner | Action |
|---|---|---|
| 1 | Agent (current tool) | Keep PROGRESS.md handoff block current at every checkpoint, so nothing is lost if the limit hits without warning |
| 2 | Operator | Confirm the working tree is clean or that the last state is recorded in the handoff block |
| 3 | Operator | Open the other tool in the same repository on the same branch |
| 4 | Operator | Start the session with the **resume prompt** from AGENT_PROMPTS.md §3 |
| 5 | New agent | Follows §7.1 exactly, including §7.5 if needed |
| 6 | New agent | Updates the handoff block with `by: cursor` or `by: codex` |

Branch hygiene: switching tools does not change the branch. A `wip/` branch is used only when the operator wants the in-progress state preserved separately from the feature branch.

### 7.7 What Agents Must Not Do

| Prohibition | Reason |
|---|---|
| Change a risk limit value | Limits are operator decisions (ADR-0009, ADR-0012) |
| Enable `TRADING_ENABLE_GROWW` or add a live-order path for Groww | PRD §12 gate G-LIVE-1 |
| Create or modify a promotion file, or enable live mode | PRD §12 gate G-LIVE-7 |
| Commit or print secrets, tokens, TOTP seeds, `.env` files, or account numbers | NFR-SEC-01 |
| Disable, skip, or weaken a test, sanitizer, lint rule, or CI check | Quality gates are the safety evidence |
| Edit a merged migration or an existing journal record version | Reproducibility and compatibility |
| Hand-edit generated code | Drift |
| Add a dependency without an ADR | TECHNICAL_IMPLEMENTATION.md §7.2 |
| Resolve an UNKNOWN order by sending a new client ID | DESIGN_ARCHITECTURE.md §5.4 |
| Put Python on the order path | ADR-0001, ADR-0002 |
| Push to `main` or merge a PR | Human merge only |
| Invent production values (capital, limits, fees, instrument specs) | Use `TODO(operator)` |

### 7.8 Quota-Aware Working

Because Codex usage limits can interrupt work:
- Prefer tickets that can finish inside one session when possible; split larger tickets.
- Run cheap checks often (`just lint`, unit tests) and expensive ones before PR (`just check` on all presets).
- Avoid repeatedly re-reading large documents. Use the section references in AGENTS.md.
- If a limit is reached during long test runs, record the test command and its status in the handoff block so the next session does not repeat work unnecessarily.

## 8. Quality Gates Summary

| Gate | Where enforced | Blocks |
|---|---|---|
| Local `just check` | Pre-commit and manual | Commit |
| CI required checks | PR | Merge |
| Class A human review | PR | Merge |
| Replay parity | CI for strategy, risk, orders, journal changes | Merge |
| Phase exit criteria | Phase review note | Start of next phase |
| PRD §12 gates | Compliance and ADR records | Live enablement |
| Release manifest verification | Release process | Live profile start |

## 9. Release Process

### 9.1 Versioning

- Engine: semantic versioning. Major: breaking journal or control changes. Minor: new features. Patch: fixes.
- Journal format: versioned independently (`format version` in record header).
- Control protocol: versioned by package (`v1`, `v2`).
- Database schema: versioned by migration sequence.

### 9.2 Release Steps

1. Tag the release on `main`: `vX.Y.Z`.
2. CI builds release artifacts: `trader`, `watchdog`, Python wheel for `tradelab.engine`, and the UI bundle.
3. Release manifest records: commit, tag, schema versions, lockfile hashes, build flags, artifact hashes.
4. Paper and testnet profiles run against the tagged build for the required soak period.
5. For live: operator creates the promotion file referencing the tag, config hash, and paper report hash. The engine verifies it at startup.

### 9.3 Rollback

- Rollback is to a previous tagged release, not to a previous commit.
- The journal is forward-compatible within a format version. Rolling back the engine does not require journal changes.
- Database rollback follows migration down-scripts on non-production data only. Production database rollback requires a documented procedure in `docs/runbooks/`.

## 10. Risk Management of the Project Itself

| Risk | Control |
|---|---|
| Agent makes a plausible but wrong change to a safety path | Class A human review; independent review pass; replay parity; sanitizers |
| Agent quota ends mid-change | Checkpoint commits; handoff block; §7.5 recovery |
| Scope creep into non-goals | Non-goals in PRD §4; agents may not add scope without a backlog entry |
| Documentation drifts from code | Docs updated in Definition of Done; quarterly review by operator |
| Operator bottleneck on reviews | Class A limited to safety-critical paths; Class B/C reviews kept fast; reviews scheduled daily |
| Schedule slips | Phase gates; no date commitments beyond the current phase |

## 11. Meetings, Reviews, and Cadence

| Activity | Frequency | Participants | Output |
|---|---|---|---|
| Session planning | Start of each work session | Operator and agent | Active ticket selected in PROGRESS.md |
| Session review | End of each work session | Operator | Reviewed PRs; PROGRESS.md current |
| Weekly review | Weekly | Operator | Backlog reprioritized; blockers resolved; risks updated |
| Phase review | End of each phase | Operator | `docs/reviews/phase-N.md` |
| Trading review (from Phase 6) | Daily during paper and live | Operator | Daily report reviewed; incidents logged |
| Drill | Weekly from Phase 6 | Operator | Kill-switch drill recorded in `docs/reviews/drills/` |

## 12. Communication and Records

- Decisions are recorded where they are made: ADR for architecture, PROGRESS.md for in-flight questions, PR for code, `docs/compliance/` for regulatory.
- No decision lives only in a chat conversation. If a conversation produces a decision, it is written into a document in the same session.
- Incidents are recorded in `docs/incidents/YYYY-MM-DD-slug.md` within 24 hours of occurrence, using the template in USER_OPERATIONS.md §10.

## 13. Metrics for the Project

| Metric | Measured | Purpose |
|---|---|---|
| Tickets completed per phase | Per phase | Progress visibility, not a velocity target |
| Checkpoint frequency | Per session, from commit log | Detects long unchecked sessions that risk loss |
| Quota interruptions and recoveries | Per session (from handoff blocks) | Planning and tool-switch frequency |
| CI pass rate on first attempt | Weekly | Quality of agent output |
| Escaped defects (found after merge) | Per phase | Quality gate effectiveness |
| Replay parity failures | Per PR | Parity health |
| Open TODO(operator) count | Weekly | Decisions pending from the operator |
