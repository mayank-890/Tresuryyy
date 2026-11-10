"""Journal every control command (audit log / control_commands projection)."""

from __future__ import annotations

import time
from dataclasses import dataclass
from pathlib import Path

from tradelab_services.control_api.commands import CommandRequest, journal_kind, journal_scope
from tradelab_services.journal import generated as g
from tradelab_services.journal.writer import JournalWriter


@dataclass
class AuditResult:
    sequence: int
    journal_path: Path


class ControlAuditLog:
    def __init__(self, journal_path: Path) -> None:
        self._writer = JournalWriter(path=journal_path)
        self._writer.open()

    def append_accepted(self, req: CommandRequest) -> AuditResult:
        payload = g.encode_control_command(
            g.ControlCommandPayload(
                command=journal_kind(req.command),
                scope=journal_scope(req.scope),
                operator_ref=req.operator_ref[:32],
                outcome=int(g.CommandOutcome.Accepted),
            )
        )
        wall_ns = time.time_ns()
        seq = self._writer.append(
            g.RecordType.ControlCommand,
            payload,
            monotonic_ns=time.monotonic_ns(),
            wall_ns=wall_ns,
        )
        self._writer.flush()
        return AuditResult(sequence=seq, journal_path=self._writer.path)

    def close(self) -> None:
        self._writer.close()
