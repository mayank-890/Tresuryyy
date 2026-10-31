# journal_recover

Recording-path recovery for a torn v1 journal tail (FR-JR-02, AL-14).

The inspector (`journal_inspect`) stays read-only. This tool is the truncate step. It does not place orders, touch risk limits, or talk to a broker.

## When to use

The recorder stopped mid-write (crash, kill). A segment may end with a partial record. A complete record that was fsynced is kept. The incomplete tail is not.

## Operator steps

1. Stop the recorder so nothing appends to the segment during recovery.
2. Report only (no file change):

```bash
PYTHONPATH=tools:tools/journal_inspect python3 -m journal_recover path/to/segment.journal
```

3. Read the output.
   - `inspector_status=OK` and `action=none` — nothing to do.
   - `inspector_status=TORN_TAIL`, `alert=AL-14`, `action=dry-run` — a torn tail was found. The process exits 1. Prior complete records are the valid prefix.
   - `action=none` and a message about a valid `TJNF` footer — the segment was closed cleanly. Do not truncate it. Inspect the body the same way retention does (footer is 32 bytes and is not a journal record).
   - `action=refused` (exit 2) — CRC failure, sequence gap, bad magic, or another fatal scan. The file is not modified. Do not delete bytes by hand. DESIGN §10.3: a mid-file failure halts; it is not a torn tail.
4. Apply the truncate only for `TORN_TAIL` / `AL-14`:

```bash
PYTHONPATH=tools:tools/journal_inspect python3 -m journal_recover path/to/segment.journal --apply
```

   This fsyncs the original bytes to `segment.journal.torn`, writes the verified prefix, and atomically replaces the segment. Exit 0 prints `alert=AL-14` and `action=applied`.
5. Run the command again without `--apply`. Expect `inspector_status=OK`.
6. Keep the `.torn` file until the prefix has been checked. A second `--apply` refuses if that backup exists and its bytes differ from the current segment.
7. This tool does not resume trading and does not reconcile with a broker. Engine restart and reconciliation are out of scope here.

Exit codes: `0` unchanged or truncated, `1` torn tail reported and not yet truncated, `2` refused or usage error.

TODO(operator): production journal root and who may run `--apply` on a live recording host (Q-17).
