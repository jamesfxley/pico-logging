// SPDX-License-Identifier: MIT
#pragma once

/// \file pico_logging.hpp
/// pico-logging umbrella header. Pulls in the full public surface — the
/// PICO_LOG_* macros and the Sink seam behind them — so a consumer can include
/// this alone and log.

// The umbrella re-exports the public surface.
// IWYU pragma: begin_exports
#include "pico_logging/logging/logging.hpp"
#include "pico_logging/logging/ring_sink.hpp"
#include "pico_logging/logging/sinks.hpp"

// IWYU pragma: end_exports

namespace pico_logging {

/// Semantic version of the library, mirroring project(pico_logging VERSION ...)
/// in the root CMakeLists.txt.
/// Major version. Changes when a documented interface breaks.
inline constexpr int VERSION_MAJOR = 0;
/// Minor version. Changes when the interface grows compatibly.
inline constexpr int VERSION_MINOR = 1;
/// Patch version. Changes when behaviour is corrected within the interface.
inline constexpr int VERSION_PATCH = 0;

}  // namespace pico_logging
