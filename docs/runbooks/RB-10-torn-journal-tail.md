# RB-10 Torn journal tail (AL-14)

Recording-path procedure for FR-JR-02. No order, risk, or broker steps.

## Detection

`journal_inspect` reports `status=TORN_TAIL` and `alert=AL-14` when the segment ends in a partial header or partial payload. It does not modify the file.

`journal_recover` uses that scan. Without `--apply` it only prints the report.

## Recovery

1. Stop the recorder.
2. `PYTHONPATH=tools:tools/journal_inspect python3 -m journal_recover <segment>`
3. If `action=refused`, stop. A CRC error, sequence gap, or bad magic is not truncated.
4. If `alert=AL-14` and `action=dry-run`, re-run with `--apply`.
5. Confirm the next run prints `inspector_status=OK`. The pre-truncate bytes are in `<segment>.torn`.
6. Do not start a live profile from this procedure. Reconciliation after an engine restart is a later ticket.

A closed segment ends with a valid `TJNF` footer (32 bytes). The inspector can describe that footer as a short tail. `journal_recover` leaves a valid footer in place.

See `tools/journal_recover/README.md`.
