Tick recorder for paper and live capture (recording only — no order APIs).

## Tick capture (T-0102)

From the repository root:

```bash
export TRADELAB_JOURNAL_ROOT=/path/to/journal/segments  # TODO(operator): host path
PYTHONPATH=services/src python3 -m tradelab_services.ingestion \
  --mode mock \
  --instruments tests/fixtures/ingestion/recording_instruments.toml \
  --mock-feed tests/fixtures/delta/public_feed.ndjson
```

Modes: `mock` (NDJSON), `mock-ws` (scripted WS frames, CI-safe), `live` (Delta public WS — requires non-empty instrument list and operator Q-18).

Journal output uses **rotating segments** under `--journal-root` with TJNF footers (T-0105).
TODO(operator): confirm instrument list, retention, and Delta licensing for live capture.

Historical OHLCV backfill remains in the research package (`tradelab-ohlcv-backfill`).
