# Projector stub (FR-JR-03)

Reads a v1 journal segment and writes idempotent rows to PostgreSQL. The journal file is only read. Ingestion and recording do not call this process. If PostgreSQL is down, this command exits 3 and does not change the segment. Recording continues because it is a separate process (DESIGN §11: a database outage does not stop trading).

`event_time` is the record header `wall_ns` converted to UTC, not `now()`. `now()` is used only for `projection_checkpoint.updated_at`.

This stub uses `INSERT ... ON CONFLICT DO NOTHING` (and an order-row update that ignores an older sequence). It uses the `psql` client already used by `just test-db`, or `docker run postgres:16-bookworm psql` when `psql` is not on `PATH`. A Python driver needs an ADR (Q-23). Batched `COPY` is not in this stub.

## Run

```bash
export DATABASE_URL=postgres://...@127.0.0.1:5432/trading_dev?sslmode=disable
PYTHONPATH=services/src:tools:tools/journal_inspect \
  python3 -m tradelab_services.projector --segment path/to/segment.journal
```

Dry-run (no database connection):

```bash
PYTHONPATH=services/src:tools:tools/journal_inspect \
  python3 -m tradelab_services.projector --segment path/to/segment.journal --dry-run
```

Create partitions before a backfill (`just migrate`, then `db/partitions/create_daily_partitions.sh` and `create_monthly_partitions.sh`). The fixed fixture instant `wall_ns=1705326896789000000` is `2024-01-15 13:54:56.789+00` (integer UTC conversion) and needs a partition for that day or month. This tool does not create partitions.

Exit codes: `0` projected or nothing to write, `2` fatal segment (CRC, gap, bad magic) and no rows written, `3` database unavailable.

## What is projected

| Journal record | Table | Conflict |
|---|---|---|
| `MarketQuote`, `MarketTrade` | `market_ticks` | `(source_segment, source_sequence, event_time)` DO NOTHING |
| `OrderIntent` | `orders` | `client_order_id`; ignore an older `source_sequence` |
| `OrderEvent` | `order_events` plus status update on `orders` when that row exists | event key DO NOTHING; status update only if `source_sequence` is not older |
| `RiskRejected` | `risk_events` | source key DO NOTHING |
| `ControlCommand` | `control_commands` | `(source_segment, source_sequence)` DO NOTHING |
| `SessionStart` | `config_versions` | DO NOTHING |

Every run sorts by sequence and reports holes. Missing sequences are not inserted. A file the inspector marks `FATAL` is not partially projected. A torn tail projects the valid prefix only and does not truncate the file (`alert=AL-14`). Re-running does not add duplicate rows.

`FillEvent` and `PositionSnapshot` are not projected (Q-22): `fills` and `position_snapshots` require columns the journal payload does not carry. This stub does not invent them.

The checkpoint row id is `journal-stub`. Each run re-reads the whole segment, so the checkpoint is not a cursor that skips a hole.

TODO(operator): production `DATABASE_URL` role (`projector_writer` is `NOLOGIN` in the migration), partition coverage, and whether this process is supervised separately from ingestion.
