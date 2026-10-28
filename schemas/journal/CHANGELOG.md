# Journal schema changelog

## v1 (2026-10-29)

Initial journal format version 1.

- **Header:** 40-byte little-endian header per DESIGN_ARCHITECTURE.md §10.1 (`TJNL` magic, format version 1, CRC32C over header with CRC field zeroed plus payload).
- **Record types:** 18 payload types with IDs from TECHNICAL_IMPLEMENTATION.md §5.2 (`SessionStart` through `PromotionVerified`).
- **Reader compatibility:** Readers must skip unknown record types using `payload_length`. Unknown enum values should be preserved as raw integers in bindings.
- **Writer compatibility:** New record types may be added without bumping format version. Existing record layouts in v1 are immutable; new fields require a new record version.
