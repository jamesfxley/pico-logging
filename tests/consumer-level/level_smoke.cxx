// SPDX-License-Identifier: MIT
// The installed package, configured at a non-default ceiling. The definition
// the package publishes must arrive here as written, and it must be the
// ceiling the header applies, or it never propagated.
#include <cstdio>
#include <string>

#include "pico_logging/pico_logging.hpp"
#include "pico_logging/testing/capture_sink.hpp"

static_assert(PICO_LOGGING_DEFAULT_CEILING == 5,
              "PICO_LOGGING_DEFAULT_CEILING did not reach the consumer");
static_assert(PICO_LOGGING_EFFECTIVE_LEVEL == 5,
              "the package's ceiling is not the one the header applies");

int main() {
    pico_logging::testing::CaptureSink sink;
    pico_logging::set_sink(&sink);

    // OR-3: the runtime level is not capped by the library's own translation
    // unit, so TRACE is reachable from here.
    pico_logging::set_level(pico_logging::Level::TRACE);
    if (pico_logging::get_level() != pico_logging::Level::TRACE) {
        std::puts("set_level was capped by the library's translation unit");
        return 1;
    }

    PICO_LOG_TRACE << "installed package reachable at trace";

    pico_logging::set_sink(nullptr);
    if (sink.str().find("installed package reachable at trace") ==
        std::string::npos) {
        std::puts("the trace record never reached the sink");
        return 1;
    }
    return 0;
}
