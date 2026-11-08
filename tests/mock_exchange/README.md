Mock exchange used by tests and paper mode.

Scripted scenarios live under `scenarios/*.toml` (TI §12.3). Each file lists ordered `[[place]]`
responses and optional `[[timeline]]` disconnect/reconnect points. Load with
`trading::broker::mock::load_scenario_from_file` plus `MockExchangeBroker` construction.
