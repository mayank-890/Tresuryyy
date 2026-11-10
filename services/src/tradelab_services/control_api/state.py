"""In-memory status until telemetry shared memory is wired."""

from __future__ import annotations

import time
from dataclasses import dataclass, field


@dataclass
class ControlState:
    halted: bool = False
    last_command: str = ""
    last_journal_sequence: int = 0
    last_operator: str = ""
    updated_wall_ns: int = field(default_factory=time.time_ns)

    def apply_command(self, command: str, *, operator_ref: str, journal_sequence: int) -> None:
        self.last_command = command
        self.last_operator = operator_ref
        self.last_journal_sequence = journal_sequence
        self.updated_wall_ns = time.time_ns()
        if command in {"halt", "kill"}:
            self.halted = True
        elif command == "resume":
            self.halted = False
