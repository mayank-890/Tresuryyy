Merge instrument master seed + recording ID patch and validate symbol lists (T-0104).

```bash
./build/debug-asan/engine/instrument_enrich \
  --seed config/instruments/delta_crypto.toml \
  --patch config/instruments/delta_recording_ids.toml \
  --symbols tests/fixtures/instruments/journal_symbols.txt
```

Use `--output path.toml` to write the enriched subset when all symbols map with `instrument_id`.
