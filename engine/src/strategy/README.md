Strategy base, signal, sizing, and the parameter snapshot double buffer.

Public headers are under `engine/include/trading/strategy/`. `decide()` writes `OrderIntent` values into a caller-provided span. It does not assign a client order id and does not touch a broker, the journal writer, or a socket. `ParamDoubleBuffer::load_for_cycle()` copies the published snapshot; that copy is the decision-cycle boundary. Strategies are listed in `active_strategies.hpp`. Duplicate names fail the build. `flat_echo` is a sample strategy and is not for trading.
