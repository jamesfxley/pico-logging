// SPDX-License-Identifier: MIT
#pragma once

/// \file pico_logging.hpp
/// Umbrella header for the full public surface: the PICO_LOG_* macros and the
/// Sink seam behind them. A consumer can include this alone and log.

// IWYU pragma: begin_exports
#include "pico_logging/logging/logging.hpp"
#include "pico_logging/logging/ring_sink.hpp"
#include "pico_logging/logging/sinks.hpp"

// IWYU pragma: end_exports

namespace pico_logging {

/// Major version, mirroring project(pico_logging VERSION ...) in the root
/// CMakeLists.txt. The policy is in the README, Releases.
inline constexpr int VERSION_MAJOR = 0;
/// Minor version.
inline constexpr int VERSION_MINOR = 1;
/// Patch version.
inline constexpr int VERSION_PATCH = 0;

}  // namespace pico_logging
