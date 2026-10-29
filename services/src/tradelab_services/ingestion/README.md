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

Live Delta public WebSocket or REST polling is not enabled in CI; use mock fixtures.
TODO(operator): confirm instrument list, retention, and Delta licensing for live capture.

Historical OHLCV backfill remains in the research package (`tradelab-ohlcv-backfill`).
