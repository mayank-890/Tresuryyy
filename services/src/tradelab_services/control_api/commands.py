"""Command mapping between HTTP, UDS line protocol, and journal records."""

from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum

from tradelab_services.journal import generated as g


class HttpCommand(StrEnum):
    HALT = "halt"
    RESUME = "resume"
    CANCEL_ALL = "cancel-all"
    KILL = "kill"
    STATUS = "status"


CONFIRMATION_PHRASES: dict[HttpCommand, str] = {
    HttpCommand.HALT: "HALT",
    HttpCommand.RESUME: "RESUME",
    HttpCommand.CANCEL_ALL: "CANCEL_ALL",
    HttpCommand.KILL: "KILL",
}

REQUIRES_CONFIRMATION = frozenset(
    {
        HttpCommand.HALT,
        HttpCommand.RESUME,
        HttpCommand.KILL,
        HttpCommand.CANCEL_ALL,
    }
)


class ControlScopeName(StrEnum):
    GLOBAL = "global"
    STRATEGY = "strategy"
    INSTRUMENT = "instrument"


_SCOPE_TO_JOURNAL: dict[ControlScopeName, int] = {
    ControlScopeName.GLOBAL: int(g.ControlScope.Global),
    ControlScopeName.STRATEGY: int(g.ControlScope.Strategy),
    ControlScopeName.INSTRUMENT: int(g.ControlScope.Instrument),
}

_SCOPE_TO_LINE: dict[ControlScopeName, str] = {
    ControlScopeName.GLOBAL: "GLOBAL",
    ControlScopeName.STRATEGY: "STRATEGY",
    ControlScopeName.INSTRUMENT: "INSTRUMENT",
}

_COMMAND_TO_LINE: dict[HttpCommand, str] = {
    HttpCommand.HALT: "HALT",
    HttpCommand.RESUME: "RESUME",
    HttpCommand.CANCEL_ALL: "CANCEL_ALL",
    HttpCommand.KILL: "KILL",
}

# Journal schema has Flatten, not Kill; Kill maps to Flatten (DESIGN §12.2 global halt+cancel+flatten).
_COMMAND_TO_JOURNAL: dict[HttpCommand, int] = {
    HttpCommand.HALT: int(g.ControlCommandKind.Halt),
    HttpCommand.RESUME: int(g.ControlCommandKind.Resume),
    HttpCommand.CANCEL_ALL: int(g.ControlCommandKind.CancelAll),
    HttpCommand.KILL: int(g.ControlCommandKind.Flatten),
}


@dataclass(frozen=True)
class CommandRequest:
    command: HttpCommand
    scope: ControlScopeName
    operator_ref: str
    scope_ref: str = ""
    confirmed: bool = False


def journal_kind(command: HttpCommand) -> int:
    return _COMMAND_TO_JOURNAL[command]


def journal_scope(scope: ControlScopeName) -> int:
    return _SCOPE_TO_JOURNAL[scope]


def uds_line_payload(
    req: CommandRequest,
    *,
    session_token: str,
) -> dict[str, object]:
    return {
        "version": 1,
        "command": _COMMAND_TO_LINE[req.command],
        "scope": _SCOPE_TO_LINE[req.scope],
        "operator_ref": req.operator_ref[:32],
        "session_token": session_token,
        "confirmed": req.confirmed,
        "scope_ref": req.scope_ref,
    }
