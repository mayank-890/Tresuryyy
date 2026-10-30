# journal_retention

Recording-only journal segment status and retention apply (T-0105).

- `status --root PATH` — list closed/open segments under the journal root.
- `apply --root PATH [--max-closed-segments N] [--max-closed-age-sec S]` — prune closed segments only when `ARCHIVE_VERIFIED=1` (same guard spirit as `db/partitions/drop_partition.sh`).

TODO(operator): production size/time rotation limits and archive destination.
