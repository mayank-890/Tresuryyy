Read-only data-quality reports for v1 journal segments (T-0103).

```bash
PYTHONPATH=tools:tools/journal_inspect python3 -m journal_dq_report path/to.segment
PYTHONPATH=tools:tools/journal_inspect python3 -m journal_dq_report path/to.segment --format json
```

Uses T-0008 inspector rules for CRC/torn-tail; lenient scan for sequence-gap statistics.
