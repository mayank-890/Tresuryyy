# Portable UTC date helpers (source from bash).

partition_uses_bsd_date() {
  date -u -v+0d +%Y-%m-%d >/dev/null 2>&1
}

partition_daily_range() {
  local offset="$1"
  if partition_uses_bsd_date; then
    date -u -v+"${offset}"d +%Y-%m-%d
  else
    date -u -d "+${offset} days" +%Y-%m-%d
  fi
}

partition_month_start() {
  local offset="$1"
  python3 - "${offset}" <<'PY'
import sys
from datetime import datetime, timezone

offset = int(sys.argv[1])
now = datetime.now(timezone.utc).replace(day=1, hour=0, minute=0, second=0, microsecond=0)
month_index = now.year * 12 + (now.month - 1) + offset
year = month_index // 12
month = month_index % 12 + 1
print(f"{year:04d}-{month:02d}-01")
PY
}

partition_month_suffix() {
  local offset="$1"
  python3 - "${offset}" <<'PY'
import sys
from datetime import datetime, timezone

offset = int(sys.argv[1])
now = datetime.now(timezone.utc).replace(day=1, hour=0, minute=0, second=0, microsecond=0)
month_index = now.year * 12 + (now.month - 1) + offset
year = month_index // 12
month = month_index % 12 + 1
print(f"{year:04d}{month:02d}")
PY
}
