// Phase 0 calendar and instrument loaders (T-0011). Compiled into trading_profile.

namespace trading::calendar {

void register_stub() {}

} // namespace trading::calendar

#include "calendar_load.cpp"
#include "parse_util.cpp"
#include "session_query.cpp"
#include "validate.cpp"

#include "../instruments/instrument_load.cpp"
#include "../instruments/validate.cpp"
#include "../instruments/enrich.cpp"
