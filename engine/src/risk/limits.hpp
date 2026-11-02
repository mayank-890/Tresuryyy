// Risk limit values are operator-owned. Agents must not set values here.
// See DESIGN_ARCHITECTURE.md §7 and ADR-0009.
//
// TODO(operator): RL-01 max order notional. Unset.
// TODO(operator): RL-02 max order quantity in lots. Unset.
// TODO(operator): RL-03 max net position per instrument. Unset.
// TODO(operator): RL-04 max net position per underlying. Unset.
// TODO(operator): RL-05 max gross exposure. Unset.
// TODO(operator): RL-06 max absolute delta, gamma, and vega. Unset.
// TODO(operator): RL-07 price collar in ticks. Unset.
// TODO(operator): RL-08 order rate budget. Unset.
// TODO(operator): RL-09 max cancel-to-order ratio. Unset.
// TODO(operator): RL-10 max daily loss per strategy and portfolio. Unset.
// TODO(operator): RL-11 max drawdown from the high-water mark. Unset.
// TODO(operator): RL-12 self-trade prevention. Policy check; no numeric ceiling.
// TODO(operator): RL-13 minimum post-trade margin headroom. Unset.
// TODO(operator): RL-14 expiry-day entry block. Policy check; no numeric ceiling.
// TODO(operator): RL-15 stale-data block. Policy check; no numeric ceiling.
// TODO(operator): RL-16 reduce-only mode. Policy check; no numeric ceiling.
//
// Until those ceilings exist, RiskGate::evaluate refuses every order.
// Do not add numeric placeholders in this file.
