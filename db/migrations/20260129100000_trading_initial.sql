-- migrate:up
-- Schema trading, roles, and projected tables (DESIGN §11, TI §11.1).
-- Partitioned append tables: UNIQUE (source_segment, source_sequence, event_time); ADR-0013.
-- event_time is populated by the projector from journal header wall_ns (no DEFAULT on projected rows).

CREATE SCHEMA trading;

CREATE ROLE projector_writer NOLOGIN;
CREATE ROLE control_api_reader NOLOGIN;
CREATE ROLE report_reader NOLOGIN;

CREATE TABLE trading.projection_checkpoint (
    projector_id          text PRIMARY KEY,
    last_source_segment   text NOT NULL,
    last_source_sequence  bigint NOT NULL,
    updated_at            timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE trading.market_ticks (
    source_segment   text NOT NULL,
    source_sequence  bigint NOT NULL,
    event_time       timestamptz NOT NULL,
    instrument_id    integer NOT NULL,
    bid_ticks        bigint,
    ask_ticks        bigint,
    last_ticks       bigint,
    volume_lots      bigint,
    CONSTRAINT market_ticks_source_unique UNIQUE (source_segment, source_sequence, event_time)
) PARTITION BY RANGE (event_time);

CREATE INDEX market_ticks_event_time_brin ON trading.market_ticks USING BRIN (event_time);

CREATE TABLE trading.candles_1m (
    source_segment   text NOT NULL,
    source_sequence  bigint NOT NULL,
    event_time       timestamptz NOT NULL,
    instrument_id    integer NOT NULL,
    open_ticks       bigint NOT NULL,
    high_ticks       bigint NOT NULL,
    low_ticks        bigint NOT NULL,
    close_ticks      bigint NOT NULL,
    volume_lots      bigint NOT NULL DEFAULT 0,
    CONSTRAINT candles_1m_source_unique UNIQUE (source_segment, source_sequence, event_time)
) PARTITION BY RANGE (event_time);

CREATE INDEX candles_1m_event_time_brin ON trading.candles_1m USING BRIN (event_time);

CREATE TABLE trading.order_events (
    source_segment    text NOT NULL,
    source_sequence   bigint NOT NULL,
    event_time        timestamptz NOT NULL,
    client_order_id   text NOT NULL,
    from_status       smallint NOT NULL,
    to_status         smallint NOT NULL,
    reason_code       smallint NOT NULL DEFAULT 0,
    CONSTRAINT order_events_source_unique UNIQUE (source_segment, source_sequence, event_time)
) PARTITION BY RANGE (event_time);

CREATE INDEX order_events_event_time_brin ON trading.order_events USING BRIN (event_time);

CREATE TABLE trading.orders (
    client_order_id   text PRIMARY KEY,
    source_segment    text NOT NULL,
    source_sequence   bigint NOT NULL,
    strategy_id       integer NOT NULL,
    instrument_id     integer NOT NULL,
    side              smallint NOT NULL,
    status            smallint NOT NULL,
    qty_lots          bigint NOT NULL,
    limit_price_ticks bigint,
    updated_at        timestamptz NOT NULL,
    CONSTRAINT orders_source_unique UNIQUE (source_segment, source_sequence)
);

CREATE INDEX orders_client_order_id_idx ON trading.orders (client_order_id);

CREATE TABLE trading.fills (
    source_segment    text NOT NULL,
    source_sequence   bigint NOT NULL,
    event_time        timestamptz NOT NULL,
    strategy_id       integer NOT NULL,
    instrument_id     integer NOT NULL,
    client_order_id   text NOT NULL,
    qty_lots          bigint NOT NULL,
    price_ticks       bigint NOT NULL,
    CONSTRAINT fills_source_unique UNIQUE (source_segment, source_sequence, event_time)
) PARTITION BY RANGE (event_time);

CREATE INDEX fills_strategy_instrument_time_idx
    ON trading.fills (strategy_id, instrument_id, event_time);
CREATE INDEX fills_event_time_brin ON trading.fills USING BRIN (event_time);

CREATE TABLE trading.position_snapshots (
    source_segment    text NOT NULL,
    source_sequence   bigint NOT NULL,
    event_time        timestamptz NOT NULL,
    strategy_id       integer NOT NULL,
    instrument_id     integer NOT NULL,
    position_lots     bigint NOT NULL,
    avg_price_ticks   bigint,
    CONSTRAINT position_snapshots_source_unique UNIQUE (source_segment, source_sequence, event_time)
) PARTITION BY RANGE (event_time);

CREATE INDEX position_snapshots_event_time_brin ON trading.position_snapshots USING BRIN (event_time);

CREATE TABLE trading.risk_events (
    source_segment   text NOT NULL,
    source_sequence  bigint NOT NULL,
    event_time       timestamptz NOT NULL,
    event_type       text NOT NULL,
    detail           text NOT NULL DEFAULT '',
    CONSTRAINT risk_events_source_unique UNIQUE (source_segment, source_sequence, event_time)
) PARTITION BY RANGE (event_time);

CREATE INDEX risk_events_event_time_brin ON trading.risk_events USING BRIN (event_time);

CREATE TABLE trading.control_commands (
    source_segment   text NOT NULL,
    source_sequence  bigint NOT NULL,
    event_time       timestamptz NOT NULL,
    operator_id      text NOT NULL,
    command_type     text NOT NULL,
    payload          text NOT NULL DEFAULT '',
    CONSTRAINT control_commands_source_unique UNIQUE (source_segment, source_sequence)
);

CREATE INDEX control_commands_event_time_brin ON trading.control_commands USING BRIN (event_time);

CREATE TABLE trading.config_versions (
    source_segment   text NOT NULL,
    source_sequence  bigint NOT NULL,
    event_time       timestamptz NOT NULL,
    content_hash     text NOT NULL,
    profile_name     text NOT NULL,
    CONSTRAINT config_versions_content_hash_unique UNIQUE (content_hash),
    CONSTRAINT config_versions_source_unique UNIQUE (source_segment, source_sequence)
);

CREATE TABLE trading.audit_log (
    source_segment   text NOT NULL,
    source_sequence  bigint NOT NULL,
    event_time       timestamptz NOT NULL,
    event_type       text NOT NULL,
    detail           text NOT NULL DEFAULT '',
    CONSTRAINT audit_log_source_unique UNIQUE (source_segment, source_sequence, event_time)
) PARTITION BY RANGE (event_time);

CREATE INDEX audit_log_event_time_brin ON trading.audit_log USING BRIN (event_time);

GRANT USAGE ON SCHEMA trading TO projector_writer, control_api_reader, report_reader;

GRANT INSERT ON ALL TABLES IN SCHEMA trading TO projector_writer;
GRANT SELECT ON ALL TABLES IN SCHEMA trading TO projector_writer;
GRANT UPDATE ON trading.orders TO projector_writer;
GRANT UPDATE ON trading.projection_checkpoint TO projector_writer;

GRANT SELECT ON ALL TABLES IN SCHEMA trading TO control_api_reader;
GRANT SELECT ON ALL TABLES IN SCHEMA trading TO report_reader;

ALTER DEFAULT PRIVILEGES IN SCHEMA trading
    GRANT INSERT, SELECT ON TABLES TO projector_writer;
ALTER DEFAULT PRIVILEGES IN SCHEMA trading
    GRANT SELECT ON TABLES TO control_api_reader, report_reader;

-- migrate:down

DROP SCHEMA IF EXISTS trading CASCADE;

DROP ROLE IF EXISTS projector_writer;
DROP ROLE IF EXISTS control_api_reader;
DROP ROLE IF EXISTS report_reader;
