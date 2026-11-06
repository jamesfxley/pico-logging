// SPDX-License-Identifier: MIT
// Compiled at LOGGING_GLOBAL_LEVEL=4. quiet_unit.cpp is the same source
// compiled at 1, and level_odr_test.cpp links both.
// OR-2

#include "level_units.hpp"
#include "pico_logging/logging/logging.hpp"

namespace level_odr {

void log_debug_from_verbose_unit() { PICO_LOG_DEBUG << VERBOSE_MESSAGE; }

void log_error_from_verbose_unit() { PICO_LOG_ERROR << "verbose error"; }

}  // namespace level_odr
