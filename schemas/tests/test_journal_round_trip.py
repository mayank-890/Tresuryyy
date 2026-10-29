"""Journal v1 encode/decode round-trip tests (T-0004)."""

from __future__ import annotations

import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GEN_PATH = ROOT / "services/src/tradelab_services/journal/generated.py"


def _load_generated():
    spec = importlib.util.spec_from_file_location("journal_generated", GEN_PATH)
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


g = _load_generated()


class JournalRoundTripTests(unittest.TestCase):
    def test_header_layout_constants(self) -> None:
        self.assertEqual(g.RECORD_HEADER_SIZE, 40)
        self.assertEqual(g.JOURNAL_MAGIC, "TJNL")

    def test_header_round_trip(self) -> None:
        payload = g.encode_session_start(
            g.SessionStartPayload(
                profile_name="paper-demo",
                config_hash=bytes([0x11] * 32),
                engine_build_hash=bytes([0x22] * 32),
                client_id_prefix="S7F2",
            )
        )
        header = g.RecordHeader(
            magic=b"TJNL",
            format_version=1,
            record_type=int(g.RecordType.SessionStart),
            sequence=42,
            monotonic_ns=100,
            wall_ns=200,
            payload_length=len(payload),
            crc32c=0,
        )
        encoded = g.encode_header(header, payload)
        self.assertEqual(len(encoded), 40)
        decoded = g.decode_header(encoded)
        self.assertEqual(decoded.sequence, 42)
        self.assertEqual(decoded.payload_length, len(payload))

    def _round_trip(self, original, encode_fn, decode_fn) -> None:
        encoded = encode_fn(original)
        decoded = decode_fn(encoded)
        self.assertEqual(decoded, original)

    def test_session_start(self) -> None:
        self._round_trip(
            g.SessionStartPayload("paper-demo", bytes([0x11] * 32), bytes([0x22] * 32), "S7F2"),
            g.encode_session_start,
            g.decode_session_start,
        )

    def test_session_end(self) -> None:
        self._round_trip(
            g.SessionEndPayload(g.SessionEndReason.NormalShutdown, g.EngineRunState.Running),
            g.encode_session_end,
            g.decode_session_end,
        )

    def test_market_quote(self) -> None:
        self._round_trip(
            g.MarketQuotePayload(7, 1000, 1005, 2, 3, 999),
            g.encode_market_quote,
            g.decode_market_quote,
        )

    def test_market_trade(self) -> None:
        self._round_trip(
            g.MarketTradePayload(7, 1002, 1, g.Side.Buy, 1000),
            g.encode_market_trade,
            g.decode_market_trade,
        )

    def test_feed_state(self) -> None:
        self._round_trip(
            g.FeedStatePayload(7, g.FeedHealthState.Live, 0),
            g.encode_feed_state,
            g.decode_feed_state,
        )

    def test_order_intent(self) -> None:
        self._round_trip(
            g.OrderIntentPayload("S7F2-0000000001", 3, 7, g.Side.Buy, 2, 1000, g.TimeInForce.Day, 1),
            g.encode_order_intent,
            g.decode_order_intent,
        )

    def test_approval_issued(self) -> None:
        self._round_trip(
            g.ApprovalIssuedPayload("S7F2-0000000001", 9, 500_000_000),
            g.encode_approval_issued,
            g.decode_approval_issued,
        )

    def test_risk_rejected(self) -> None:
        self._round_trip(
            g.RiskRejectedPayload("S7F2-0000000001", 2, 11, 10),
            g.encode_risk_rejected,
            g.decode_risk_rejected,
        )

    def test_order_sent(self) -> None:
        self._round_trip(
            g.OrderSentPayload("S7F2-0000000001", g.EndpointCategory.OrderEntry),
            g.encode_order_sent,
            g.decode_order_sent,
        )

    def test_order_event(self) -> None:
        self._round_trip(
            g.OrderEventPayload(
                "S7F2-0000000001",
                g.OrderState.PendingNew,
                g.OrderState.Acked,
                "BRK-123",
                0,
            ),
            g.encode_order_event,
            g.decode_order_event,
        )

    def test_fill_event(self) -> None:
        self._round_trip(
            g.FillEventPayload("S7F2-0000000001", 99, 1001, 1, 1, 1002),
            g.encode_fill_event,
            g.decode_fill_event,
        )

    def test_position_snapshot(self) -> None:
        self._round_trip(
            g.PositionSnapshotPayload(7, 1, 1001, -5),
            g.encode_position_snapshot,
            g.decode_position_snapshot,
        )

    def test_reconciliation_result(self) -> None:
        self._round_trip(
            g.ReconciliationResultPayload(
                g.ReconciliationScope.Global, g.ReconciliationOutcome.Clean, 0, "ok"
            ),
            g.encode_reconciliation_result,
            g.decode_reconciliation_result,
        )

    def test_control_command(self) -> None:
        self._round_trip(
            g.ControlCommandPayload(
                g.ControlCommandKind.Halt,
                g.ControlScope.Global,
                "operator-ref-1",
                g.CommandOutcome.Accepted,
            ),
            g.encode_control_command,
            g.decode_control_command,
        )

    def test_limit_tightened(self) -> None:
        self._round_trip(
            g.LimitTightenedPayload(3, 100, 90, "operator-ref-1"),
            g.encode_limit_tightened,
            g.decode_limit_tightened,
        )

    def test_state_transition(self) -> None:
        self._round_trip(
            g.StateTransitionPayload(g.StateDomain.Engine, 1, 2, 0),
            g.encode_state_transition,
            g.decode_state_transition,
        )

    def test_config_recorded(self) -> None:
        self._round_trip(
            g.ConfigRecordedPayload(bytes([0x33] * 32), bytes([0x44] * 32)),
            g.encode_config_recorded,
            g.decode_config_recorded,
        )

    def test_promotion_verified(self) -> None:
        self._round_trip(
            g.PromotionVerifiedPayload(
                bytes([0x55] * 32), bytes([0x66] * 32), g.VerificationOutcome.Passed
            ),
            g.encode_promotion_verified,
            g.decode_promotion_verified,
        )

    def test_cpp_python_session_start_payload_match(self) -> None:
        py_bytes = g.encode_session_start(
            g.SessionStartPayload("paper-demo", bytes([0x11] * 32), bytes([0x22] * 32), "S7F2")
        )
        binary = ROOT / "build/journal_round_trip_test"
        if not binary.exists():
            self.skipTest("C++ journal test binary not built")
        proc = subprocess.run(
            [str(binary), "--print-python-hex"],
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc.stdout.strip(), py_bytes.hex())

    def test_research_generated_matches_services(self) -> None:
        research = (ROOT / "research/src/tradelab/journal/generated.py").read_text(encoding="utf-8")
        services = GEN_PATH.read_text(encoding="utf-8")
        self.assertEqual(research, services)


if __name__ == "__main__":
    result = unittest.main(verbosity=2, exit=False)
    sys.exit(0 if result.result.wasSuccessful() else 1)
