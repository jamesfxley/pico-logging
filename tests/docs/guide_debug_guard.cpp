// SPDX-License-Identifier: MIT
// The usage guide's runtime level guard, around a summary that is costly to
// build.
#include <pico_logging/logging/logging.hpp>
#include <string>

namespace doc_examples {
std::string expensive_summary();

void guide_debug_guard() {
#include "guide_debug_guard.inc"
}
}  // namespace doc_examples
