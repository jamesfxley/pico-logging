// SPDX-License-Identifier: MIT
// FR-3: a value the 64-bit and double paths cannot hold is not rendered as a
// different value. Built with GNU extensions, CMake's default for a consumer,
// because that is where a 128-bit integer is an integral type at all: under
// strict C++20 it takes the placeholder without reaching the integer path.
#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <string>
#include <type_traits>

#include "pico_logging/logging/logging.hpp"
#include "pico_logging/logging/sinks.hpp"
#include "pico_logging/testing/capture_sink.hpp"

namespace {

using pico_logging::testing::CaptureSink;

struct SinkGuard {
    explicit SinkGuard(pico_logging::Sink* sink) {
        pico_logging::set_sink(sink);
    }

    ~SinkGuard() { pico_logging::set_sink(nullptr); }

    SinkGuard(const SinkGuard&) = delete;
    SinkGuard& operator=(const SinkGuard&) = delete;
    SinkGuard(SinkGuard&&) = delete;
    SinkGuard& operator=(SinkGuard&&) = delete;
};

TEST(WideValueTest, AnIntegerWiderThan64BitsRendersAsAPlaceholder) {
#ifdef __SIZEOF_INT128__
    __extension__ using Wide = unsigned __int128;
    static_assert(std::is_integral_v<Wide>,
                  "this target is built with GNU extensions");
    CaptureSink sink;
    const SinkGuard guard(&sink);
    const auto wide = static_cast<Wide>(5U) | (static_cast<Wide>(1U) << 64U);
    PICO_LOG_WARN << "[" << wide << "]";
    EXPECT_NE(sink.str().find("[<?>]"), std::string::npos) << sink.str();
#else
    GTEST_SKIP() << "no 128-bit integer on this target";
#endif
}

TEST(WideValueTest, ALongDoubleBeyondDoubleRendersAsAPlaceholder) {
    if constexpr (sizeof(long double) > sizeof(double)) {
        CaptureSink sink;
        const SinkGuard guard(&sink);
        const auto rendered = [&sink](long double value) {
            sink.clear();
            PICO_LOG_WARN << "[" << value << "]";
            const std::string& out = sink.str();
            const std::size_t open = out.rfind('[');
            const std::size_t close = out.rfind(']');
            return open != std::string::npos && close != std::string::npos
                       ? out.substr(open + 1, close - open - 1)
                       : out;
        };
        // Too large and too small for double: each would print a different
        // value, infinity or zero.
        EXPECT_EQ(rendered(std::numeric_limits<long double>::max()), "<?>");
        EXPECT_EQ(rendered(std::numeric_limits<long double>::denorm_min()),
                  "<?>");
        // Infinity is not out of range: it renders as itself (FR-5).
        EXPECT_EQ(rendered(std::numeric_limits<long double>::infinity()),
                  "inf");
        EXPECT_EQ(rendered(-std::numeric_limits<long double>::infinity()),
                  "-inf");
        EXPECT_EQ(rendered(0.0L), "0.000000");
    } else {
        GTEST_SKIP() << "long double is double on this target";
    }
}

}  // namespace
