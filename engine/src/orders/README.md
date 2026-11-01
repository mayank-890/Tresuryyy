Order state machine, client order ids, and the in-memory order book.

`apply_transition` accepts only DESIGN §5.2. Invalid transitions are logged, recorded as defects, and halt that strategy and instrument. `ClientOrderIdGenerator` issues `<prefix>-<10 digits>` after the counter journal accepts. `OrderBook::send` calls the send port only after the intent journal and the sent-transition journal both succeed. `resolve_unknown` follows DESIGN §5.4: query the existing client id up to three times, then resend that same id once.
