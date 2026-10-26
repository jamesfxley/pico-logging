// SPDX-License-Identifier: MIT
#include <cstddef>
#include <source_location>
#include <span>
#include <string_view>

#include "pico_logging/logging/logging.hpp"

// AR-1: compile-only object, so debug string formatting adds no image code.
int main(int argc, char** argv) {
    const std::span args(argv, static_cast<std::size_t>(argc));
    if (args.empty()) {
        return 0;
    }
    pico_logging::LogLine line(pico_logging::Level::INFO,
                               std::source_location::current());
    line << std::string_view(args.front());
}
