// SPDX-License-Identifier: MIT
// OR-2. Compiled at LOGGING_GLOBAL_LEVEL=1; see verbose_unit.cpp, which is the
// same source compiled at 4, and level_odr_test.cpp, which links both.

#include "level_units.hpp"
#include "pico_logging/logging/logging.hpp"

namespace level_odr {

void log_debug_from_quiet_unit() { PICO_LOG_DEBUG << QUIET_MESSAGE; }

void log_error_from_quiet_unit() { PICO_LOG_ERROR << "quiet error"; }

}  // namespace level_odr
