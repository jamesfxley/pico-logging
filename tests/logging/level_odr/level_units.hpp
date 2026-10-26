// SPDX-License-Identifier: MIT
#pragma once

#include <string_view>

/// \file level_units.hpp
/// Shared declarations for the two translation units of the OR-2 regression.

namespace level_odr {

inline constexpr std::string_view VERBOSE_MESSAGE = "verbose unit debug";
inline constexpr std::string_view QUIET_MESSAGE = "quiet unit debug";

/// Defined in verbose_unit.cpp, compiled at LOGGING_GLOBAL_LEVEL=4.
void log_debug_from_verbose_unit();
void log_error_from_verbose_unit();

/// Defined in quiet_unit.cpp, compiled at LOGGING_GLOBAL_LEVEL=1.
void log_debug_from_quiet_unit();
void log_error_from_quiet_unit();

}  // namespace level_odr
