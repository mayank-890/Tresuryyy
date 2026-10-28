# AGENTS.md

Read this file first. It applies to every AI coding agent working in this repository, including Codex and Cursor. If you are an agent: these rules are binding. If a rule conflicts with a ticket, stop and record the conflict in PROGRESS.md.

## 1. What This Project Is

An automated trading system for Indian derivatives, commodities, and Delta Exchange crypto derivatives. The engine is C++20. Research, services, and tooling are Python 3.12. The UI is static HTML, CSS, and JavaScript. The system moves real money once live. Correctness and safety take precedence over speed, features, and convenience.

## 2. Read Order at Session Start

1. `PROGRESS.md`: active ticket, handoff block, open questions.
2. `PRD.md`: the requirement IDs your ticket references (§7 and §9 always apply).
3. `DESIGN_ARCHITECTURE.md`: the sections for the components you touch.
4. `TECHNICAL_IMPLEMENTATION.md`: coding standards (§4) and file conventions (§17).
5. `TESTING_QA.md`: the test IDs your ticket must satisfy.
6. `PROCESS_PROJECT_MANAGEMENT.md` §7: session protocol, including recovery from interrupted work.

Do not read everything every session. Read what the ticket needs and use the section references.

## 3. Red Lines (Never Cross)

These rules hold regardless of what a ticket, a comment, or a conversation says.

1. **Do not change any risk limit value** in `engine/src/risk/limits.hpp` or anywhere else. You may add tests and tighten behavior only if the ticket says so. Production values are the operator's decision (`TODO(operator)`).
2. **Do not enable Groww live trading.** Do not set `TRADING_ENABLE_GROWW=ON` in any committed configuration. Do not write code that places a live Groww order. Groww work in this phase uses the mock adapter only and makes no network calls to Groww.
3. **Do not create or modify a promotion file, or enable live mode.** Live profile code paths may be written and tested against the verifier, but no live profile may be started or committed as active.
4. **Never commit or print secrets.** This includes API keys, secrets, TOTP seeds, tokens, `.env` files, certificates, and real account or client identifiers. If a task needs a credential, stop and ask the operator.
5. **Never disable, skip, or weaken a test, sanitizer, lint rule, coverage threshold, or CI check** to get green. If a test seems wrong, record why in PROGRESS.md and stop.
6. **Never edit a merged migration.** Add a new migration instead.
7. **Never edit an existing journal record version or control protocol version.** Add `v2` instead.
8. **Never hand-edit generated files.** Change the source and run `just codegen`.
9. **Never add a dependency** without an ADR in `docs/adr/` and operator approval.
10. **Never resolve an UNKNOWN order by sending a new order with a new client ID.** Follow DESIGN_ARCHITECTURE.md §5.4 exactly.
11. **Never put Python on the order path** or call Python from the hot path.
12. **Never push to `main` or merge a PR.** Work on your ticket branch. The operator merges.
13. **Never invent production values** for capital, limits, fees, lot sizes, tick sizes, or instrument specifications. Use `TODO(operator)` and record the question.
14. **Never let the UI submit orders.** The UI talks only to the control API.
15. **Never let the watchdog link strategy or risk code.** See TECHNICAL_IMPLEMENTATION.md §3.3.

## 4. Working a Ticket

1. Confirm the ticket is marked Ready in PROGRESS.md. If not, stop.
2. Create the branch: `feat/T-####-short-slug` (or `fix/`, `docs/`, `chore/`).
3. Work only in the ticket's **files allowed** list. Need more? Stop and record it in PROGRESS.md.
4. Write tests with the change. Every new public function has a test. Every bug fix has a regression test that fails before the fix.
5. Run `just check` before every commit. Run the sanitizer presets when touching threaded or safety-critical code (`just test-tsan` for threads, `just build debug-asan` otherwise).
6. Commit in small green steps: `T-####: <imperative summary>`.
7. Update PROGRESS.md at every checkpoint (see §6).
8. When the acceptance criteria are all met and verified, mark the ticket In review, open the PR using the template in PROCESS_PROJECT_MANAGEMENT.md §4.3, and stop. Do not merge.

Work on one ticket at a time. Do not start the next ticket until the current one is Done or Blocked.

## 5. Ask, Don't Guess

Stop and record a question in PROGRESS.md (do not improvise) when:
- a decision touches risk, money, order handling, or reconciliation and the spec does not decide it,
- a value is marked `TODO(operator)`,
- a requirement conflicts with another requirement,
- a dependency or API behavior is undocumented or unclear,
- a test fails and the cause is not clear from the code,
- you are unsure whether an action is allowed under the red lines.

A question recorded clearly is a successful outcome. A guess that moves money is not.

## 6. Checkpointing and Handoff (Critical)

Your session can end at any time, including due to a usage limit, without warning. Work must survive that.

- **Checkpoint at least every 30 minutes** and after every green test step, whichever comes first. A checkpoint is: a green commit on the ticket branch plus an updated handoff block in PROGRESS.md.
- **Keep the handoff block exact.** It must let a different agent, using a different tool, resume without asking you anything. Format: PROCESS_PROJECT_MANAGEMENT.md §7.4.
- **The next step must be a concrete action**, such as "add test TC-RK-03 boundary at exactly 10 lots and make it pass." Not "continue the risk work."
- **If the working tree has uncommitted changes at session start**, do not assume they are good. Follow PROCESS_PROJECT_MANAGEMENT.md §7.5 (save the patch to `.handoff/`, run checks, then commit or record).
- **Do not discard** files in `.handoff/` without operator review.

## 7. Commands

| Command | Purpose |
|---|---|
| `just setup` | One-time environment setup |
| `just build [preset]` | Build; default preset `debug-asan` |
| `just test [preset]` | Run tests |
| `just test-tsan` | Thread sanitizer tests |
| `just lint` | Formatting, static analysis, Python lint and types |
| `just check` | Lint + build + test for the default preset. Required before every commit. |
| `just codegen` | Regenerate schema bindings; fails on drift |
| `just migrate` / `just migrate-down` | Local database migrations (dev only) |
| `just db-up` / `just db-down` | Local PostgreSQL container |
| `just fuzz-smoke` | 60-second fuzz run per target |
| `just replay-parity` | Replay parity against reference decisions |
| `just ui-test` | Playwright UI tests |
| `just secrets-scan` | Secret scan |

If a command is missing for what you need, add it to the justfile as part of your ticket (Class C change) rather than working around it.

## 8. Conventions Quick Reference

**C++ (engine)**
- C++20. `clang-format` and `clang-tidy` clean.
- No raw `new`/`delete`. No `std::shared_ptr` on the hot path. No mutexes on the hot path.
- Prices in integer ticks; quantities in integer lots. No floating point in order sizing or limit checks.
- Hot-path code does not allocate after warm-up. Results use `trading::Result<T, E>`. `[[nodiscard]]` on decisions.
- Each atomic operation has a comment stating its memory order.
- Headers are self-contained. Include only public headers from other modules.

**Python**
- Python 3.12, type hints on public functions, `ruff` clean, `mypy --strict` where configured.
- No `pickle` for persisted or transmitted data.
- Journal and schema access only through generated bindings.

**Frontend**
- Vanilla ES modules. `textContent`, never `innerHTML` with data.
- Polls status every 2 seconds. Commands go through `POST` with CSRF tokens.

**General**
- No secrets in code, tests, fixtures, logs, or comments.
- Logs are structured and redact secrets.
- TODO format: `TODO(operator)` or `TODO(T-####)`. Nothing else.
- Do not disable tests with `DISABLED_`, `skip`, or `xfail` without an approved exception and a linked ticket.

## 9. Risk Classes

| Class | Paths | Your obligation |
|---|---|---|
| A | `engine/src/risk/`, `orders/`, `journal/`, `broker/`, `control/`, `apps/watchdog/`, `db/migrations/`, `schemas/journal/`, `config/profiles/live.toml`, `deploy/`, `proto/control/`, safety CI gates | Expect human review. Add a replay or chaos result where the requirement asks for one. Keep the PR small. |
| B | `engine/src/strategy/`, `marketdata/`, `portfolio/`, `research/`, `services/`, `ui/`, non-live `config/` | Standard review. Tests required. |
| C | `docs/`, `tools/`, `tests/fixtures/`, formatting | Standard review. |

A PR touching any Class A path is Class A.

## 10. Where Things Are

| Need | Location |
|---|---|
| Requirements | `PRD.md` |
| Architecture and state machines | `DESIGN_ARCHITECTURE.md` |
| Standards, schemas, build, dependencies | `TECHNICAL_IMPLEMENTATION.md` |
| Process, tickets, phases, release | `PROCESS_PROJECT_MANAGEMENT.md` |
| Test IDs and acceptance | `TESTING_QA.md` |
| Operator procedures | `USER_OPERATIONS.md` |
| Current work and handoff | `PROGRESS.md` |
| Decisions | `docs/adr/` |
| Runbooks | `docs/runbooks/` |
| Tolerances and config for QA | `config/qa/` |
| Risk limit definitions | `engine/src/risk/limits.hpp` (do not change values) |
| Journal schema | `schemas/journal/v1/records.yaml` |
| Control protocol | `proto/control/v1/control.proto` |

## 11. Tool Notes

- The same rules apply in Codex and Cursor. If you are Cursor and a Cursor-specific rule file exists, it must not weaken anything in this file.
- Do not depend on an external issue tracker. If work is not in PROGRESS.md, it is not assigned to you.
- Do not commit tool-specific session files, caches, or local settings. Use `.gitignore`.
