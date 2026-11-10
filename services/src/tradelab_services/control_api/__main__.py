"""Run the localhost control API (`python -m tradelab_services.control_api`)."""

from __future__ import annotations

import argparse
import os
from pathlib import Path

from tradelab_services.control_api.app import ControlApiConfig, serve_forever
from tradelab_services.control_api.auth import AuthConfig


def main() -> None:
    parser = argparse.ArgumentParser(description="Trading control API (localhost only)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--journal", type=Path, default=Path(os.environ.get("TRADELAB_JOURNAL_ROOT", ".")) / "control-api.journal")
    parser.add_argument("--control-socket", default=os.environ.get("TRADING_CONTROL_SOCKET", ""))
    parser.add_argument("--operator-id", default=os.environ.get("TRADING_OPERATOR_ID", "operator"))
    args = parser.parse_args()
    token = os.environ.get("TRADING_CONTROL_API_TOKEN", "")
    if not token:
        msg = "TRADING_CONTROL_API_TOKEN must be set"
        raise SystemExit(msg)
    config = ControlApiConfig(
        host=args.host,
        port=args.port,
        journal_path=args.journal,
        control_socket=args.control_socket,
        operator_id=args.operator_id,
        auth=AuthConfig(bearer_token=token, csrf_token=os.environ.get("TRADING_CONTROL_CSRF_TOKEN", token[:24])),
    )
    serve_forever(config)


if __name__ == "__main__":
    main()
