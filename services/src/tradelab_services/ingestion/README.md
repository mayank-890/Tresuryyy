Tick recorder for paper and live capture, and backfill jobs.

Historical OHLCV backfill (Delta public REST → Parquet) is implemented in the research
package for Phase 1: run `uv run tradelab-ohlcv-backfill` from `research/` with
`TRADELAB_DATA_ROOT` set. Live tick capture to the journal is a later Phase 1 deliverable.
