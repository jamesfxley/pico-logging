// SPDX-License-Identifier: MIT
// The README's runtime level guard, around a summary that is costly to build.
#include <pico_logging/logging/logging.hpp>
#include <string>

namespace doc_examples {
std::string expensive_summary();

void readme_debug_guard() {
#include "readme_debug_guard.inc"
}
}  // namespace doc_examples
