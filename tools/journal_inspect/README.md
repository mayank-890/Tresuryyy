Read-only journal validator and printer (journal v1).

Run on a segment file:

```bash
python3 tools/journal_inspect/__main__.py tests/fixtures/compat/v1/normal.segment
```

Tests and fixture generation:

```bash
bash tools/journal_inspect/run_tests.sh
```

`--repair` is not implemented; files are never modified.
