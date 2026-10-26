// SPDX-License-Identifier: MIT
// Compiles the umbrella header as a translation unit so the full public surface
// is exercised by the analysis toolchain, and pins the advertised version to
// the values the build system carries.
//
// This file deliberately includes *only* the umbrella: it is the one place that
// proves a consumer can drive the library from that single include, so a public
// header dropped from it is a build failure here rather than a surprise
// downstream.
//
// misc-include-cleaner therefore has to be off for the file. Every symbol used
// below reaches this TU transitively, which is exactly the property under test;
// naming the providing headers directly would delete the test.
// NOLINTBEGIN(misc-include-cleaner)
#include "pico_logging/pico_logging.hpp"

#include <gtest/gtest.h>

#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

// A sink written against the umbrella's Sink alone, so nothing here names a
// header the umbrella should have brought in.
class Recorder final : public pico_logging::Sink {
   public:
    void write(std::string_view text) override { text_.append(text); }

    [[nodiscard]] const std::string& text() const { return text_; }

   private:
    std::string text_;
};

}  // namespace

// The advertised version and project(pico_logging VERSION ...) are two records
// of one fact, compared here against the value CMake holds rather than against
// a literal copied out of the header -- which would only ever compare the
// header with itself. src/logging/sinks.cpp static_asserts the same pair, so
// the drift is caught by whichever tree gets built first.
TEST(PicoLogging, VersionMatchesTheBuildSystem) {
    EXPECT_EQ(pico_logging::VERSION_MAJOR, PICO_LOGGING_CMAKE_VERSION_MAJOR);
    EXPECT_EQ(pico_logging::VERSION_MINOR, PICO_LOGGING_CMAKE_VERSION_MINOR);
    EXPECT_EQ(pico_logging::VERSION_PATCH, PICO_LOGGING_CMAKE_VERSION_PATCH);
}

// The umbrella alone must be enough to install a sink and emit through it.
TEST(PicoLogging, UmbrellaHeaderAloneCanLog) {
    Recorder sink;
    pico_logging::set_sink(&sink);

    PICO_LOG_ERROR << "umbrella";

    pico_logging::set_sink(nullptr);
    EXPECT_NE(sink.text().find("umbrella"), std::string::npos);
}

// Every public sink reaches this file through the umbrella: a header dropped
// from it fails this build.
TEST(PicoLogging, UmbrellaHeaderReachesEverySink) {
    static_assert(
        std::is_base_of_v<pico_logging::Sink, pico_logging::StderrSink>);

    Recorder target;
    std::mutex lock;
    pico_logging::RingSink<2, std::mutex> ring(target, lock);
    pico_logging::set_sink(&ring);

    PICO_LOG_ERROR << "through the ring";
    ring.drain();

    pico_logging::set_sink(nullptr);
    EXPECT_NE(target.text().find("through the ring"), std::string::npos);
}

// Closes the region opened at the include above; see the reason there.
// NOLINTEND(misc-include-cleaner)
