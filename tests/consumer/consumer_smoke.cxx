// SPDX-License-Identifier: MIT
// Consumer-side view of the installed package. Nothing here reaches into the
// source tree: every symbol below must arrive through the installed
// pico_logging::core target, resolved by find_package, so a header the
// package fails to ship is a compile error in this test rather than a discovery
// made by the first downstream project.
//
// Built against the package at its default ceiling. The non-default ceiling is
// the declared case "level-five" (tests/consumer-level), which proves the
// definition propagated; this program proves the default package is usable.
#include <cstdio>
#include <string>
#include <string_view>

#include "pico_logging/pico_logging.hpp"
#include "pico_logging/testing/capture_sink.hpp"

static_assert(pico_logging::VERSION_MAJOR == 0 &&
              pico_logging::VERSION_MINOR == 1);

// The package adds no ceiling of its own at the default configuration, so the
// header's ceiling (4, DBG) applies.
static_assert(PICO_LOGGING_EFFECTIVE_LEVEL == 4,
              "the default package changed the consumer's ceiling");

int main() {
    std::printf("pico_logging %d.%d.%d\n", pico_logging::VERSION_MAJOR,
                pico_logging::VERSION_MINOR, pico_logging::VERSION_PATCH);

    pico_logging::testing::CaptureSink sink;
    pico_logging::set_sink(&sink);

    // OR-3: the runtime level is not capped by the library's own translation
    // unit, so DEBUG is reachable from here.
    pico_logging::set_level(pico_logging::Level::DBG);
    if (pico_logging::get_level() != pico_logging::Level::DBG) {
        std::puts("set_level was capped by the library's translation unit");
        return 1;
    }

    PICO_LOG_DEBUG << "installed package reachable at debug";

    pico_logging::set_sink(nullptr);
    if (sink.str().find("installed package reachable at debug") ==
        std::string::npos) {
        std::puts("the debug record never reached the sink");
        return 1;
    }
    return 0;
}
