"""Truncate a torn journal tail only after the inspector reports TORN_TAIL.

The inspector stays read-only. This module mutates a segment only when
``apply=True``. A mid-file CRC failure, sequence gap, or bad magic (FATAL)
is never truncated. A valid TJNF segment footer is left in place.

Scope (operator, 2026-10-31): copy-safe truncate. The original bytes are
fsynced to ``<segment>.torn`` before the segment is atomically replaced
with the verified prefix. Recording path only — no orders, risk, or broker.
"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path

_TOOLS = Path(__file__).resolve().parents[1]
_INSPECT = _TOOLS / "journal_inspect"
for _entry in (str(_TOOLS), str(_INSPECT)):
    if _entry not in sys.path:
        sys.path.insert(0, _entry)

from journal_retention.footer import FOOTER_SIZE, decode_footer  # noqa: E402
from segment import InspectStatus, inspect_segment  # noqa: E402


class RecoveryAction(StrEnum):
    NONE = "none"
    DRY_RUN = "dry-run"
    APPLIED = "applied"
    REFUSED = "refused"


@dataclass(frozen=True)
class RecoveryReport:
    inspector_status: InspectStatus
    action: RecoveryAction
    message: str
    alert_code: str | None
    records_kept: int
    valid_prefix_length: int
    torn_suffix_length: int
    backup_path: Path | None = None


def backup_path_for(path: Path) -> Path:
    return path.parent / f"{path.name}.torn"


def _temp_path(path: Path) -> Path:
    return path.parent / f"{path.name}.recovering"


def _refused(
    inspector_status: InspectStatus,
    message: str,
    *,
    alert_code: str | None = None,
    records_kept: int = 0,
    valid_prefix_length: int = 0,
    torn_suffix_length: int = 0,
) -> RecoveryReport:
    return RecoveryReport(
        inspector_status=inspector_status,
        action=RecoveryAction.REFUSED,
        message=message,
        alert_code=alert_code,
        records_kept=records_kept,
        valid_prefix_length=valid_prefix_length,
        torn_suffix_length=torn_suffix_length,
    )


def _closed_footer_report(data: bytes, footer_at: int) -> RecoveryReport | None:
    """A 32-byte TJNF footer looks like a torn header to the v1 inspector."""
    suffix = data[footer_at:]
    if len(suffix) != FOOTER_SIZE or decode_footer(suffix) is None:
        return None
    body = data[:footer_at]
    body_report = inspect_segment(body)
    if body_report.status is not InspectStatus.OK:
        return _refused(
            InspectStatus.FATAL,
            (
                "valid TJNF footer present but segment body is not OK "
                f"({body_report.message}); file left unchanged"
            ),
            records_kept=len(body_report.records),
            valid_prefix_length=len(body),
            torn_suffix_length=len(suffix),
        )
    return RecoveryReport(
        inspector_status=InspectStatus.TORN_TAIL,
        action=RecoveryAction.NONE,
        message=(
            f"valid segment footer (TJNF); closed segment left unchanged ({body_report.message})"
        ),
        alert_code=None,
        records_kept=len(body_report.records),
        valid_prefix_length=len(data),
        torn_suffix_length=0,
    )


def plan_recovery(data: bytes) -> tuple[bytes | None, RecoveryReport]:
    """Return the verified prefix and a report. Does not touch the filesystem."""
    report = inspect_segment(data)
    if report.status is InspectStatus.OK:
        return None, RecoveryReport(
            inspector_status=report.status,
            action=RecoveryAction.NONE,
            message=report.message,
            alert_code=None,
            records_kept=len(report.records),
            valid_prefix_length=len(data),
            torn_suffix_length=0,
        )
    if report.status is not InspectStatus.TORN_TAIL or report.error_offset is None:
        return None, _refused(
            report.status,
            report.message,
            alert_code=report.alert_code,
            records_kept=len(report.records),
        )

    closed = _closed_footer_report(data, report.error_offset)
    if closed is not None:
        return None, closed

    prefix = data[: report.error_offset]
    verified = inspect_segment(prefix)
    if verified.status is not InspectStatus.OK:
        return None, _refused(
            InspectStatus.FATAL,
            f"refusing truncate: candidate prefix is not OK ({verified.message})",
            records_kept=len(verified.records),
            valid_prefix_length=len(prefix),
            torn_suffix_length=len(data) - len(prefix),
        )
    return prefix, RecoveryReport(
        inspector_status=InspectStatus.TORN_TAIL,
        action=RecoveryAction.DRY_RUN,
        message=report.message,
        alert_code="AL-14",
        records_kept=len(verified.records),
        valid_prefix_length=len(prefix),
        torn_suffix_length=len(data) - len(prefix),
    )


def _write_durable(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o644)
    try:
        view = data
        while view:
            written = os.write(fd, view)
            if written <= 0:
                msg = f"short write to {path}"
                raise OSError(msg)
            view = view[written:]
        os.fsync(fd)
    finally:
        os.close(fd)


def _fsync_directory(directory: Path) -> None:
    fd = os.open(directory, os.O_RDONLY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def apply_recovery(path: Path, *, apply: bool) -> RecoveryReport:
    """Inspect ``path``. With ``apply=False``, report only. With ``apply=True``, truncate."""
    data = path.read_bytes()
    prefix, planned = plan_recovery(data)
    if planned.action is not RecoveryAction.DRY_RUN or prefix is None:
        return planned
    if not apply:
        return planned

    backup = backup_path_for(path)
    if backup.exists():
        if backup.read_bytes() != data:
            return _refused(
                InspectStatus.FATAL,
                f"refusing truncate: torn backup already exists and differs: {backup}",
                alert_code="AL-14",
                records_kept=planned.records_kept,
                valid_prefix_length=planned.valid_prefix_length,
                torn_suffix_length=planned.torn_suffix_length,
            )
    else:
        _write_durable(backup, data)
        if backup.read_bytes() != data:
            return _refused(
                InspectStatus.FATAL,
                f"refusing truncate: torn backup verification failed: {backup}",
                alert_code="AL-14",
                records_kept=planned.records_kept,
                valid_prefix_length=planned.valid_prefix_length,
                torn_suffix_length=planned.torn_suffix_length,
            )

    temp = _temp_path(path)
    try:
        _write_durable(temp, prefix)
        verified_bytes = temp.read_bytes()
        verified = inspect_segment(verified_bytes)
        if verified_bytes != prefix or verified.status is not InspectStatus.OK:
            return _refused(
                InspectStatus.FATAL,
                "refusing truncate: recovered prefix failed re-inspection",
                alert_code="AL-14",
                records_kept=planned.records_kept,
                valid_prefix_length=planned.valid_prefix_length,
                torn_suffix_length=planned.torn_suffix_length,
            )
        os.replace(temp, path)
        try:
            _fsync_directory(path.parent)
        except OSError:
            # The prefix file was already fsynced. Directory fsync is best-effort.
            pass
    finally:
        if temp.exists():
            temp.unlink()

    after = path.read_bytes()
    after_report = inspect_segment(after)
    if after != prefix or after_report.status is not InspectStatus.OK:
        _write_durable(path, data)
        return _refused(
            InspectStatus.FATAL,
            "truncate failed verification; original bytes restored from memory",
            alert_code="AL-14",
            records_kept=planned.records_kept,
            valid_prefix_length=planned.valid_prefix_length,
            torn_suffix_length=planned.torn_suffix_length,
        )

    return RecoveryReport(
        inspector_status=InspectStatus.TORN_TAIL,
        action=RecoveryAction.APPLIED,
        message=planned.message,
        alert_code="AL-14",
        records_kept=planned.records_kept,
        valid_prefix_length=planned.valid_prefix_length,
        torn_suffix_length=planned.torn_suffix_length,
        backup_path=backup,
    )


def format_report(report: RecoveryReport) -> str:
    lines = [
        f"inspector_status={report.inspector_status.name}",
        f"action={report.action.value}",
        f"records_kept={report.records_kept}",
        f"valid_prefix_bytes={report.valid_prefix_length}",
        f"torn_suffix_bytes={report.torn_suffix_length}",
        f"message={report.message}",
    ]
    if report.alert_code is not None:
        lines.insert(2, f"alert={report.alert_code}")
    if report.backup_path is not None:
        lines.append(f"backup={report.backup_path}")
    return "\n".join(lines) + "\n"


def exit_code(report: RecoveryReport) -> int:
    if report.action is RecoveryAction.REFUSED:
        return 2
    if report.action is RecoveryAction.DRY_RUN:
        return 1
    return 0
