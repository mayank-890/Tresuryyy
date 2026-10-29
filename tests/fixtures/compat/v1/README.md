# Journal v1 compatibility fixtures (synthetic)

Small segment files for the journal inspector (T-0008). Not for trading.

- `normal.segment` — two valid records (SessionStart, SessionEnd)
- `torn_tail.segment` — truncated mid second record (AL-14)
- `crc_corrupt.segment` — CRC failure on the first record payload

Regenerate with `python3 tools/journal_inspect/build_fixtures.py`.
