# AGENT_PROMPTS.md

Copy-paste prompts for starting and resuming work with Codex (primary) and Cursor (fallback). Only the operator uses this file. Agents should not read it during a session; AGENTS.md is their entry point.

---

## 1. Codex Kickoff Prompt (Phase 0, Use First)

Paste into a new Codex session opened at the repository root on a fresh branch.

```
You are the implementation agent for this repository: an automated derivatives and commodities trading system. Money is at risk once it goes live, so correctness and safety come before speed or features.

Before doing anything else:
1. Read AGENTS.md completely. Its red lines are binding.
2. Read PROGRESS.md. Find the Handoff block and the active ticket.
3. Run: git status; git branch --show-current; git log --oneline -5
4. If the working tree is dirty, follow PROCESS_PROJECT_MANAGEMENT.md §7.5 before anything else.

Your scope in this session: Phase 0 only, tickets in this order: T-0001, T-0002, T-0003, T-0004, T-0005, T-0006, T-0007, T-0008, T-0009, T-0010, T-0011.

Rules for this session:
- Work one ticket at a time. Create the branch named in the ticket. Finish a ticket (acceptance criteria verified, PR opened) before starting the next.
- Only edit files in the ticket's "Files allowed" list. If you need another file, stop and record it in PROGRESS.md.
- Do not change any risk limit value. Do not enable TRADING_ENABLE_GROWW. Do not create a promotion file or start live mode. Do not write Groww network code.
- Do not invent production values. Use TODO(operator) and record the question in PROGRESS.md.
- Never print or commit secrets, tokens, TOTP seeds, or account identifiers.
- Do not weaken or skip tests, sanitizers, lint, or CI checks. If a check seems wrong, record why and stop.
- Do not add dependencies beyond those named in the ticket. Anything else needs an ADR; write it as Proposed and stop.
- Do not push to main. Do not merge PRs. Open PRs only.
- Write tests with each change. Run `just check` before every commit. Commit in small green steps with messages beginning "T-####:".

Checkpoint discipline (critical, your session can end at any time):
- After every green step: commit, then update the Handoff block in PROGRESS.md with the exact next step.
- At least every 30 minutes of work, do the same.
- Keep the Handoff block in the format from PROCESS_PROJECT_MANAGEMENT.md §7.4 so that Cursor can resume without asking you anything.

Stop conditions (stop, record the question in PROGRESS.md, and end the session cleanly):
- A decision involves risk, money, order handling, reconciliation, secrets, or compliance.
- A requirement conflicts with another requirement or with a red line.
- A test fails for a reason you cannot explain.
- You reach the end of the Phase 0 backlog.

Start now with T-0001. Report at the end of the session: tickets completed, PRs opened, open questions raised, and the exact next step recorded in the Handoff block.
```

---

## 2. Cursor Kickoff Prompt (Same Task, Use Only If Codex Is Unavailable)

Paste into Cursor's agent mode, with the repository open at the root. Attach `AGENTS.md` and `PROGRESS.md` to the context if the agent does not read them automatically.

```
You are the implementation agent for this repository, taking over from another agent. The repository is an automated derivatives and commodities trading system. Money is at risk once live, so correctness and safety come before speed or features.

Read these in order before any action:
1. AGENTS.md (binding rules, red lines)
2. PROGRESS.md (handoff block, active ticket, open questions)
3. PROCESS_PROJECT_MANAGEMENT.md §7 (session protocol)

Then run: git status; git branch --show-current; git log --oneline -5. If the working tree is dirty, follow PROCESS_PROJECT_MANAGEMENT.md §7.5 first: save the diff to .handoff/, run `just check`, and decide with the rules there whether to commit or record it.

Scope: Phase 0 tickets in PROGRESS.md, in order, starting from the active ticket in the handoff block. One ticket at a time. Finish each to its acceptance criteria before starting the next.

Hard rules (same as AGENTS.md; repeated here because they matter most):
- Do not change risk limit values. Do not enable Groww. Do not create promotion files or start live mode. Do not write code that contacts Groww.
- Do not invent production values. Use TODO(operator) and record the question.
- Never print or commit secrets or account identifiers.
- Do not disable or weaken tests, sanitizers, lint, or CI checks.
- Edit only files listed as "Files allowed" in the active ticket.
- Do not add dependencies outside the ticket. Propose ADRs; do not adopt them.
- Do not push to main or merge. Open PRs only.

Checkpointing: after every green step, commit with "T-####: <summary>" and update the Handoff block in PROGRESS.md with the exact next action. Do this at least every 30 minutes. Assume the session may end at any moment.

Stop and record a question in PROGRESS.md instead of guessing when a decision involves risk, money, orders, reconciliation, secrets, or compliance, or when a requirement conflicts.

Begin by reporting the active ticket from the handoff block and the exact next step you will take. Then proceed.
```

---

## 3. Resume Prompt (Either Tool, After a Switch)

Use this whenever you move work from one tool to the other, or start a new session mid-ticket. It works for either agent.

```
Resume work on this repository. You are the implementation agent. Another agent (Codex or Cursor) was working here and its session ended, possibly mid-change.

Steps, in order, without skipping any:
1. Read AGENTS.md.
2. Read PROGRESS.md, especially the Handoff block. The "Next step (exact)" line is your starting action.
3. Run: git status; git branch --show-current; git log --oneline -5.
4. Decide the state:
   - Clean tree, last commit matches the handoff block: go to step 6.
   - Clean tree, last commit differs from the handoff block: record the discrepancy in PROGRESS.md and treat the git log as truth; then go to step 6.
   - Dirty tree: do NOT build on it yet. Run `git diff > .handoff/recovered-$(date +%Y%m%d-%H%M).patch`, then `just check`.
     - If checks pass and the diff matches the active ticket: commit with the message "T-####: (recovered) <summary>" and continue.
     - If checks fail: keep the patch file, reset the tree to the last green commit ONLY after confirming the patch is saved, and record the failure in PROGRESS.md.
     - If the diff touches files outside the ticket's files-allowed list: do not commit it. Record it and ask the operator.
5. Confirm the ticket's acceptance criteria are still the ones written in PROGRESS.md. If they differ, stop and ask.
6. Continue with the exact next step. Work on the ticket only.

Rules that remain in force (AGENTS.md §3 red lines apply in full):
- No risk limit changes. No Groww enablement. No promotion files or live mode. No secrets. No disabled tests. No pushes to main. No merges.
- No new dependencies without an ADR. No invented production values.

Checkpoint after every green step: commit, then update the Handoff block in PROGRESS.md (Updated, Active ticket, Branch, Last green commit, Working tree, Tests passing, Next step (exact), Files touched, Decisions, Open questions, Notes). Set "Updated" to the current time and your tool name.

Before ending the session, make sure the Handoff block lets a different tool resume without asking anything.
```

---

## 4. Handoff Block Format (Reference)

This is the required format in PROGRESS.md. Agents must keep it exact.

```markdown
## Handoff
- Updated: YYYY-MM-DDTHH:MM+05:30 by <codex | cursor | human>
- Active ticket: T-####
- Branch: <branch name>
- Last green commit: <short sha> "<commit message>"
- Working tree: <clean | dirty: patch saved as .handoff/<file>>
- Tests passing at last commit: <exact commands run and result>
- Next step (exact): <one concrete action, e.g. "Add TC-CF-03 test; make it fail; implement the check">
- Files touched this ticket: <list>
- Decisions made this session (not yet in ADR): <list or none>
- Open questions: <list or none>
- Notes for the next tool: <anything that would otherwise be rediscovered>
```

---

## 5. Operator Procedure: Switching From Codex to Cursor

Follow these steps in order. Do this when Codex reports a usage limit or stops responding.

1. **Check the handoff block** in PROGRESS.md. If the agent updated it within the last checkpoint, you are safe. If not, go to step 2.
2. **Check the working tree** in your terminal: `git status`. If there are uncommitted changes, do not discard them. Keep them for step 4.
3. **Open the repository in Cursor**, on the same branch as the ticket. Do not create a new branch unless you want a `wip/` copy: `git switch -c wip/T-####-cursor-$(date +%Y%m%d)` (optional).
4. **Start Cursor's agent with the Resume Prompt (§3).** If the tree is dirty, the prompt tells Cursor to save the patch and verify before committing. Do not do this recovery by hand unless the prompt fails.
5. **Watch the first checkpoint.** Confirm Cursor commits a green state and updates the Handoff block with `by: cursor`.
6. **Record the switch** in the Session Log table of PROGRESS.md: date, tool, ticket, result.
7. **When Codex is available again,** start it with the Resume Prompt (§3), not the Kickoff Prompt. Its first action is to read the handoff block.

**Do not** run both agents on the same branch at the same time. Two agents committing to one branch will produce conflicting handoff blocks. If you want parallel work, create separate ticket branches and make sure the tickets do not share files.

---

## 6. Operator Checklist Before Each Session

- [ ] PROGRESS.md shows the ticket you intend to work on, as Ready
- [ ] Branch exists or will be created by the agent per the ticket
- [ ] Working tree is clean (`git status`)
- [ ] Your local tools are working: `just --version`, `cmake --version`, `uv --version` (after T-0003)
- [ ] No live credentials are present in the environment (`env | grep -i -E 'key|secret|token'` returns nothing sensitive)
- [ ] You know which tool you are starting with (Codex by default)

## 7. Operator Checklist After Each Session

- [ ] Handoff block updated and dated
- [ ] Any new open question has a clear owner
- [ ] PR opened for any ticket marked In review; you have reviewed the Class A ones or scheduled review
- [ ] Session Log has an entry
- [ ] No secrets appeared in the session transcript or the diff; run `just secrets-scan` if unsure
