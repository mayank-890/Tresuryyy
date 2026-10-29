"""Generated journal v1 bindings. Do not edit."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum

JOURNAL_MAGIC = 'TJNL'
FORMAT_VERSION = 1
RECORD_HEADER_SIZE = 40

def compute_crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 & -(crc & 1))
    return (~crc) & 0xFFFFFFFF

class SessionEndReason(IntEnum):
    NormalShutdown = 1
    OperatorKill = 2
    WatchdogIntervention = 3
    FatalError = 4

class EngineRunState(IntEnum):
    Starting = 1
    Running = 2
    Halting = 3
    Halted = 4

class FeedHealthState(IntEnum):
    Live = 1
    Stale = 2
    Disconnected = 3

class Side(IntEnum):
    Unknown = 0
    Buy = 1
    Sell = 2

class TimeInForce(IntEnum):
    Day = 1
    ImmediateOrCancel = 2
    FillOrKill = 3

class EndpointCategory(IntEnum):
    OrderEntry = 1
    OrderQuery = 2
    Cancel = 3

class OrderState(IntEnum):
    IntentJournaled = 1
    PendingNew = 2
    Acked = 3
    PartiallyFilled = 4
    Filled = 5
    PendingCancel = 6
    Cancelled = 7
    Rejected = 8
    Expired = 9
    Unknown = 10

class ReconciliationScope(IntEnum):
    Global = 1
    Instrument = 2
    Strategy = 3

class ReconciliationOutcome(IntEnum):
    Clean = 1
    Mismatch = 2
    Aborted = 3

class ControlCommandKind(IntEnum):
    Halt = 1
    Resume = 2
    CancelAll = 3
    Flatten = 4

class ControlScope(IntEnum):
    Global = 1
    Strategy = 2
    Instrument = 3

class CommandOutcome(IntEnum):
    Accepted = 1
    Rejected = 2
    Failed = 3

class StateDomain(IntEnum):
    Engine = 1
    Feed = 2

class VerificationOutcome(IntEnum):
    Passed = 1
    Failed = 2

class RecordType(IntEnum):
    SessionStart = 1
    SessionEnd = 2
    MarketQuote = 16
    MarketTrade = 17
    FeedState = 18
    OrderIntent = 32
    ApprovalIssued = 33
    RiskRejected = 34
    OrderSent = 35
    OrderEvent = 36
    FillEvent = 37
    PositionSnapshot = 48
    ReconciliationResult = 49
    ControlCommand = 64
    LimitTightened = 65
    StateTransition = 66
    ConfigRecorded = 80
    PromotionVerified = 81

@dataclass
class RecordHeader:
    magic: bytes
    format_version: int
    record_type: int
    sequence: int
    monotonic_ns: int
    wall_ns: int
    payload_length: int
    crc32c: int

def encode_header(header: RecordHeader, payload: bytes) -> bytes:
    body = struct.pack('<4sHHQQQI', header.magic, header.format_version,
                         header.record_type, header.sequence, header.monotonic_ns,
                         header.wall_ns, len(payload))
    body += struct.pack('<I', 0)
    crc = compute_crc32c(body + payload)
    return body[:36] + struct.pack('<I', crc)

def decode_header(data: bytes) -> RecordHeader:
    if len(data) < RECORD_HEADER_SIZE:
        raise ValueError('short header')
    magic, fmt, rtype, seq, mono, wall, plen, crc = struct.unpack_from('<4sHHQQQII', data, 0)
    return RecordHeader(magic, fmt, rtype, seq, mono, wall, plen, crc)

@dataclass
class SessionStartPayload:
    profile_name: str
    config_hash: bytes
    engine_build_hash: bytes
    client_id_prefix: str

def encode_session_start(payload: SessionStartPayload) -> bytes:
    out = bytearray()
    raw = payload.profile_name.encode('utf-8')
    if len(raw) > 64: raise ValueError('profile_name')
    out += struct.pack('<H', len(raw)) + raw
    if len(payload.config_hash) != 32:
        raise ValueError('config_hash')
    out += payload.config_hash
    if len(payload.engine_build_hash) != 32:
        raise ValueError('engine_build_hash')
    out += payload.engine_build_hash
    out += struct.pack('4s', payload.client_id_prefix.encode('ascii')[:4].ljust(4, b'\0'))
    return bytes(out)

def decode_session_start(data: bytes) -> SessionStartPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    (ulen,) = struct.unpack_from('<H', data, offset)
    offset += 2
    kwargs['profile_name'] = data[offset:offset+ulen].decode('utf-8')
    offset += ulen
    kwargs['config_hash'] = data[offset:offset+32]
    offset += 32
    kwargs['engine_build_hash'] = data[offset:offset+32]
    offset += 32
    raw = struct.unpack_from('4s', data, offset)[0]
    kwargs['client_id_prefix'] = raw.rstrip(b'\0').decode('ascii')
    offset += 4
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return SessionStartPayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class SessionEndPayload:
    reason: int
    final_state: int

def encode_session_end(payload: SessionEndPayload) -> bytes:
    out = bytearray()
    out += struct.pack('<H', int(payload.reason))
    out += struct.pack('<B', int(payload.final_state))
    return bytes(out)

def decode_session_end(data: bytes) -> SessionEndPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    (val,) = struct.unpack_from('<H', data, offset)
    kwargs['reason'] = val
    offset += struct.calcsize('<H')
    (val,) = struct.unpack_from('<B', data, offset)
    kwargs['final_state'] = val
    offset += struct.calcsize('<B')
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return SessionEndPayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class MarketQuotePayload:
    instrument_id: int
    bid_ticks: int
    ask_ticks: int
    bid_size_lots: int
    ask_size_lots: int
    exchange_time_ns: int

def encode_market_quote(payload: MarketQuotePayload) -> bytes:
    out = bytearray()
    out += struct.pack('<I', payload.instrument_id)
    out += struct.pack('<q', payload.bid_ticks)
    out += struct.pack('<q', payload.ask_ticks)
    out += struct.pack('<i', payload.bid_size_lots)
    out += struct.pack('<i', payload.ask_size_lots)
    out += struct.pack('<Q', payload.exchange_time_ns)
    return bytes(out)

def decode_market_quote(data: bytes) -> MarketQuotePayload:
    offset = 0
    kwargs: dict[str, object] = {}
    (val,) = struct.unpack_from('<I', data, offset)
    kwargs['instrument_id'] = val
    offset += struct.calcsize('<I')
    (val,) = struct.unpack_from('<q', data, offset)
    kwargs['bid_ticks'] = val
    offset += struct.calcsize('<q')
    (val,) = struct.unpack_from('<q', data, offset)
    kwargs['ask_ticks'] = val
    offset += struct.calcsize('<q')
    (val,) = struct.unpack_from('<i', data, offset)
    kwargs['bid_size_lots'] = val
    offset += struct.calcsize('<i')
    (val,) = struct.unpack_from('<i', data, offset)
    kwargs['ask_size_lots'] = val
    offset += struct.calcsize('<i')
    (val,) = struct.unpack_from('<Q', data, offset)
    kwargs['exchange_time_ns'] = val
    offset += struct.calcsize('<Q')
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return MarketQuotePayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class MarketTradePayload:
    instrument_id: int
    price_ticks: int
    size_lots: int
    side: int
    exchange_time_ns: int

def encode_market_trade(payload: MarketTradePayload) -> bytes:
    out = bytearray()
    out += struct.pack('<I', payload.instrument_id)
    out += struct.pack('<q', payload.price_ticks)
    out += struct.pack('<i', payload.size_lots)
    out += struct.pack('<B', int(payload.side))
    out += struct.pack('<Q', payload.exchange_time_ns)
    return bytes(out)

def decode_market_trade(data: bytes) -> MarketTradePayload:
    offset = 0
    kwargs: dict[str, object] = {}
    (val,) = struct.unpack_from('<I', data, offset)
    kwargs['instrument_id'] = val
    offset += struct.calcsize('<I')
    (val,) = struct.unpack_from('<q', data, offset)
    kwargs['price_ticks'] = val
    offset += struct.calcsize('<q')
    (val,) = struct.unpack_from('<i', data, offset)
    kwargs['size_lots'] = val
    offset += struct.calcsize('<i')
    (val,) = struct.unpack_from('<B', data, offset)
    kwargs['side'] = val
    offset += struct.calcsize('<B')
    (val,) = struct.unpack_from('<Q', data, offset)
    kwargs['exchange_time_ns'] = val
    offset += struct.calcsize('<Q')
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return MarketTradePayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class FeedStatePayload:
    instrument_id: int
    state: int
    reason_code: int

def encode_feed_state(payload: FeedStatePayload) -> bytes:
    out = bytearray()
    out += struct.pack('<I', payload.instrument_id)
    out += struct.pack('<B', int(payload.state))
    out += struct.pack('<H', payload.reason_code)
    return bytes(out)

def decode_feed_state(data: bytes) -> FeedStatePayload:
    offset = 0
    kwargs: dict[str, object] = {}
    (val,) = struct.unpack_from('<I', data, offset)
    kwargs['instrument_id'] = val
    offset += struct.calcsize('<I')
    (val,) = struct.unpack_from('<B', data, offset)
    kwargs['state'] = val
    offset += struct.calcsize('<B')
    (val,) = struct.unpack_from('<H', data, offset)
    kwargs['reason_code'] = val
    offset += struct.calcsize('<H')
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return FeedStatePayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class OrderIntentPayload:
    client_id: str
    strategy_id: int
    instrument_id: int
    side: int
    qty_lots: int
    price_ticks: int
    time_in_force: int
    param_version: int

def encode_order_intent(payload: OrderIntentPayload) -> bytes:
    out = bytearray()
    out += payload.client_id.encode('ascii')[:24].ljust(24, b'\0')
    out += struct.pack('<I', payload.strategy_id)
    out += struct.pack('<I', payload.instrument_id)
    out += struct.pack('<B', int(payload.side))
    out += struct.pack('<i', payload.qty_lots)
    out += struct.pack('<q', payload.price_ticks)
    out += struct.pack('<B', int(payload.time_in_force))
    out += struct.pack('<I', payload.param_version)
    return bytes(out)

def decode_order_intent(data: bytes) -> OrderIntentPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    raw = struct.unpack_from('24s', data, offset)[0]
    kwargs['client_id'] = raw.rstrip(b'\0').decode('ascii')
    offset += 24
    (val,) = struct.unpack_from('<I', data, offset)
    kwargs['strategy_id'] = val
    offset += struct.calcsize('<I')
    (val,) = struct.unpack_from('<I', data, offset)
    kwargs['instrument_id'] = val
    offset += struct.calcsize('<I')
    (val,) = struct.unpack_from('<B', data, offset)
    kwargs['side'] = val
    offset += struct.calcsize('<B')
    (val,) = struct.unpack_from('<i', data, offset)
    kwargs['qty_lots'] = val
    offset += struct.calcsize('<i')
    (val,) = struct.unpack_from('<q', data, offset)
    kwargs['price_ticks'] = val
    offset += struct.calcsize('<q')
    (val,) = struct.unpack_from('<B', data, offset)
    kwargs['time_in_force'] = val
    offset += struct.calcsize('<B')
    (val,) = struct.unpack_from('<I', data, offset)
    kwargs['param_version'] = val
    offset += struct.calcsize('<I')
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return OrderIntentPayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class ApprovalIssuedPayload:
    client_id: str
    limit_set_version: int
    expiry_mono_ns: int

def encode_approval_issued(payload: ApprovalIssuedPayload) -> bytes:
    out = bytearray()
    out += payload.client_id.encode('ascii')[:24].ljust(24, b'\0')
    out += struct.pack('<I', payload.limit_set_version)
    out += struct.pack('<Q', payload.expiry_mono_ns)
    return bytes(out)

def decode_approval_issued(data: bytes) -> ApprovalIssuedPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    raw = struct.unpack_from('24s', data, offset)[0]
    kwargs['client_id'] = raw.rstrip(b'\0').decode('ascii')
    offset += 24
    (val,) = struct.unpack_from('<I', data, offset)
    kwargs['limit_set_version'] = val
    offset += struct.calcsize('<I')
    (val,) = struct.unpack_from('<Q', data, offset)
    kwargs['expiry_mono_ns'] = val
    offset += struct.calcsize('<Q')
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return ApprovalIssuedPayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class RiskRejectedPayload:
    client_id: str
    limit_id: int
    observed: int
    threshold: int

def encode_risk_rejected(payload: RiskRejectedPayload) -> bytes:
    out = bytearray()
    out += payload.client_id.encode('ascii')[:24].ljust(24, b'\0')
    out += struct.pack('<H', payload.limit_id)
    out += struct.pack('<q', payload.observed)
    out += struct.pack('<q', payload.threshold)
    return bytes(out)

def decode_risk_rejected(data: bytes) -> RiskRejectedPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    raw = struct.unpack_from('24s', data, offset)[0]
    kwargs['client_id'] = raw.rstrip(b'\0').decode('ascii')
    offset += 24
    (val,) = struct.unpack_from('<H', data, offset)
    kwargs['limit_id'] = val
    offset += struct.calcsize('<H')
    (val,) = struct.unpack_from('<q', data, offset)
    kwargs['observed'] = val
    offset += struct.calcsize('<q')
    (val,) = struct.unpack_from('<q', data, offset)
    kwargs['threshold'] = val
    offset += struct.calcsize('<q')
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return RiskRejectedPayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class OrderSentPayload:
    client_id: str
    endpoint_category: int

def encode_order_sent(payload: OrderSentPayload) -> bytes:
    out = bytearray()
    out += payload.client_id.encode('ascii')[:24].ljust(24, b'\0')
    out += struct.pack('<B', int(payload.endpoint_category))
    return bytes(out)

def decode_order_sent(data: bytes) -> OrderSentPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    raw = struct.unpack_from('24s', data, offset)[0]
    kwargs['client_id'] = raw.rstrip(b'\0').decode('ascii')
    offset += 24
    (val,) = struct.unpack_from('<B', data, offset)
    kwargs['endpoint_category'] = val
    offset += struct.calcsize('<B')
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return OrderSentPayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class OrderEventPayload:
    client_id: str
    from_state: int
    to_state: int
    broker_order_id: str
    reason_code: int

def encode_order_event(payload: OrderEventPayload) -> bytes:
    out = bytearray()
    out += payload.client_id.encode('ascii')[:24].ljust(24, b'\0')
    out += struct.pack('<H', int(payload.from_state))
    out += struct.pack('<H', int(payload.to_state))
    out += payload.broker_order_id.encode('ascii')[:32].ljust(32, b'\0')
    out += struct.pack('<H', payload.reason_code)
    return bytes(out)

def decode_order_event(data: bytes) -> OrderEventPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    raw = struct.unpack_from('24s', data, offset)[0]
    kwargs['client_id'] = raw.rstrip(b'\0').decode('ascii')
    offset += 24
    (val,) = struct.unpack_from('<H', data, offset)
    kwargs['from_state'] = val
    offset += struct.calcsize('<H')
    (val,) = struct.unpack_from('<H', data, offset)
    kwargs['to_state'] = val
    offset += struct.calcsize('<H')
    raw = struct.unpack_from('32s', data, offset)[0]
    kwargs['broker_order_id'] = raw.rstrip(b'\0').decode('ascii')
    offset += 32
    (val,) = struct.unpack_from('<H', data, offset)
    kwargs['reason_code'] = val
    offset += struct.calcsize('<H')
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return OrderEventPayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class FillEventPayload:
    client_id: str
    fill_id: int
    price_ticks: int
    qty_lots: int
    fee_ticks: int
    exchange_time_ns: int

def encode_fill_event(payload: FillEventPayload) -> bytes:
    out = bytearray()
    out += payload.client_id.encode('ascii')[:24].ljust(24, b'\0')
    out += struct.pack('<Q', payload.fill_id)
    out += struct.pack('<q', payload.price_ticks)
    out += struct.pack('<i', payload.qty_lots)
    out += struct.pack('<q', payload.fee_ticks)
    out += struct.pack('<Q', payload.exchange_time_ns)
    return bytes(out)

def decode_fill_event(data: bytes) -> FillEventPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    raw = struct.unpack_from('24s', data, offset)[0]
    kwargs['client_id'] = raw.rstrip(b'\0').decode('ascii')
    offset += 24
    (val,) = struct.unpack_from('<Q', data, offset)
    kwargs['fill_id'] = val
    offset += struct.calcsize('<Q')
    (val,) = struct.unpack_from('<q', data, offset)
    kwargs['price_ticks'] = val
    offset += struct.calcsize('<q')
    (val,) = struct.unpack_from('<i', data, offset)
    kwargs['qty_lots'] = val
    offset += struct.calcsize('<i')
    (val,) = struct.unpack_from('<q', data, offset)
    kwargs['fee_ticks'] = val
    offset += struct.calcsize('<q')
    (val,) = struct.unpack_from('<Q', data, offset)
    kwargs['exchange_time_ns'] = val
    offset += struct.calcsize('<Q')
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return FillEventPayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class PositionSnapshotPayload:
    instrument_id: int
    net_qty_lots: int
    avg_price_ticks: int
    realized_pnl_ticks: int

def encode_position_snapshot(payload: PositionSnapshotPayload) -> bytes:
    out = bytearray()
    out += struct.pack('<I', payload.instrument_id)
    out += struct.pack('<i', payload.net_qty_lots)
    out += struct.pack('<q', payload.avg_price_ticks)
    out += struct.pack('<q', payload.realized_pnl_ticks)
    return bytes(out)

def decode_position_snapshot(data: bytes) -> PositionSnapshotPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    (val,) = struct.unpack_from('<I', data, offset)
    kwargs['instrument_id'] = val
    offset += struct.calcsize('<I')
    (val,) = struct.unpack_from('<i', data, offset)
    kwargs['net_qty_lots'] = val
    offset += struct.calcsize('<i')
    (val,) = struct.unpack_from('<q', data, offset)
    kwargs['avg_price_ticks'] = val
    offset += struct.calcsize('<q')
    (val,) = struct.unpack_from('<q', data, offset)
    kwargs['realized_pnl_ticks'] = val
    offset += struct.calcsize('<q')
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return PositionSnapshotPayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class ReconciliationResultPayload:
    scope: int
    outcome: int
    detail_code: int
    detail_message: str

def encode_reconciliation_result(payload: ReconciliationResultPayload) -> bytes:
    out = bytearray()
    out += struct.pack('<B', int(payload.scope))
    out += struct.pack('<B', int(payload.outcome))
    out += struct.pack('<H', payload.detail_code)
    raw = payload.detail_message.encode('utf-8')
    if len(raw) > 256: raise ValueError('detail_message')
    out += struct.pack('<H', len(raw)) + raw
    return bytes(out)

def decode_reconciliation_result(data: bytes) -> ReconciliationResultPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    (val,) = struct.unpack_from('<B', data, offset)
    kwargs['scope'] = val
    offset += struct.calcsize('<B')
    (val,) = struct.unpack_from('<B', data, offset)
    kwargs['outcome'] = val
    offset += struct.calcsize('<B')
    (val,) = struct.unpack_from('<H', data, offset)
    kwargs['detail_code'] = val
    offset += struct.calcsize('<H')
    (ulen,) = struct.unpack_from('<H', data, offset)
    offset += 2
    kwargs['detail_message'] = data[offset:offset+ulen].decode('utf-8')
    offset += ulen
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return ReconciliationResultPayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class ControlCommandPayload:
    command: int
    scope: int
    operator_ref: str
    outcome: int

def encode_control_command(payload: ControlCommandPayload) -> bytes:
    out = bytearray()
    out += struct.pack('<H', int(payload.command))
    out += struct.pack('<B', int(payload.scope))
    out += payload.operator_ref.encode('ascii')[:32].ljust(32, b'\0')
    out += struct.pack('<B', int(payload.outcome))
    return bytes(out)

def decode_control_command(data: bytes) -> ControlCommandPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    (val,) = struct.unpack_from('<H', data, offset)
    kwargs['command'] = val
    offset += struct.calcsize('<H')
    (val,) = struct.unpack_from('<B', data, offset)
    kwargs['scope'] = val
    offset += struct.calcsize('<B')
    raw = struct.unpack_from('32s', data, offset)[0]
    kwargs['operator_ref'] = raw.rstrip(b'\0').decode('ascii')
    offset += 32
    (val,) = struct.unpack_from('<B', data, offset)
    kwargs['outcome'] = val
    offset += struct.calcsize('<B')
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return ControlCommandPayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class LimitTightenedPayload:
    limit_id: int
    old_value: int
    new_value: int
    operator_ref: str

def encode_limit_tightened(payload: LimitTightenedPayload) -> bytes:
    out = bytearray()
    out += struct.pack('<H', payload.limit_id)
    out += struct.pack('<q', payload.old_value)
    out += struct.pack('<q', payload.new_value)
    out += payload.operator_ref.encode('ascii')[:32].ljust(32, b'\0')
    return bytes(out)

def decode_limit_tightened(data: bytes) -> LimitTightenedPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    (val,) = struct.unpack_from('<H', data, offset)
    kwargs['limit_id'] = val
    offset += struct.calcsize('<H')
    (val,) = struct.unpack_from('<q', data, offset)
    kwargs['old_value'] = val
    offset += struct.calcsize('<q')
    (val,) = struct.unpack_from('<q', data, offset)
    kwargs['new_value'] = val
    offset += struct.calcsize('<q')
    raw = struct.unpack_from('32s', data, offset)[0]
    kwargs['operator_ref'] = raw.rstrip(b'\0').decode('ascii')
    offset += 32
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return LimitTightenedPayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class StateTransitionPayload:
    domain: int
    from_state: int
    to_state: int
    reason_code: int

def encode_state_transition(payload: StateTransitionPayload) -> bytes:
    out = bytearray()
    out += struct.pack('<B', int(payload.domain))
    out += struct.pack('<H', payload.from_state)
    out += struct.pack('<H', payload.to_state)
    out += struct.pack('<H', payload.reason_code)
    return bytes(out)

def decode_state_transition(data: bytes) -> StateTransitionPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    (val,) = struct.unpack_from('<B', data, offset)
    kwargs['domain'] = val
    offset += struct.calcsize('<B')
    (val,) = struct.unpack_from('<H', data, offset)
    kwargs['from_state'] = val
    offset += struct.calcsize('<H')
    (val,) = struct.unpack_from('<H', data, offset)
    kwargs['to_state'] = val
    offset += struct.calcsize('<H')
    (val,) = struct.unpack_from('<H', data, offset)
    kwargs['reason_code'] = val
    offset += struct.calcsize('<H')
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return StateTransitionPayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class ConfigRecordedPayload:
    config_hash: bytes
    content_ref_hash: bytes

def encode_config_recorded(payload: ConfigRecordedPayload) -> bytes:
    out = bytearray()
    if len(payload.config_hash) != 32:
        raise ValueError('config_hash')
    out += payload.config_hash
    if len(payload.content_ref_hash) != 32:
        raise ValueError('content_ref_hash')
    out += payload.content_ref_hash
    return bytes(out)

def decode_config_recorded(data: bytes) -> ConfigRecordedPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    kwargs['config_hash'] = data[offset:offset+32]
    offset += 32
    kwargs['content_ref_hash'] = data[offset:offset+32]
    offset += 32
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return ConfigRecordedPayload(**kwargs)  # type: ignore[arg-type]

@dataclass
class PromotionVerifiedPayload:
    promotion_file_hash: bytes
    config_hash: bytes
    outcome: int

def encode_promotion_verified(payload: PromotionVerifiedPayload) -> bytes:
    out = bytearray()
    if len(payload.promotion_file_hash) != 32:
        raise ValueError('promotion_file_hash')
    out += payload.promotion_file_hash
    if len(payload.config_hash) != 32:
        raise ValueError('config_hash')
    out += payload.config_hash
    out += struct.pack('<B', int(payload.outcome))
    return bytes(out)

def decode_promotion_verified(data: bytes) -> PromotionVerifiedPayload:
    offset = 0
    kwargs: dict[str, object] = {}
    kwargs['promotion_file_hash'] = data[offset:offset+32]
    offset += 32
    kwargs['config_hash'] = data[offset:offset+32]
    offset += 32
    (val,) = struct.unpack_from('<B', data, offset)
    kwargs['outcome'] = val
    offset += struct.calcsize('<B')
    if offset != len(data):
        raise ValueError('payload length mismatch')
    return PromotionVerifiedPayload(**kwargs)  # type: ignore[arg-type]
