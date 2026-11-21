// SPDX-License-Identifier: MIT
// Includes only the umbrella header, so the analysis toolchain sees the full
// public surface and a public header dropped from it fails this build. Also
// pins the advertised version to the build system's.
//
// misc-include-cleaner has to be off for the file. Every symbol used below
// reaches this TU transitively, which is the property under test; naming the
// providing headers directly would delete the test.
// NOLINTBEGIN(misc-include-cleaner)
#include "pico_logging/pico_logging.hpp"

#include <gtest/gtest.h>

#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

// Written against the umbrella's Sink alone.
class Recorder final : public pico_logging::Sink {
   public:
    void write(std::string_view text) override { text_.append(text); }

    [[nodiscard]] const std::string& text() const { return text_; }

   private:
    std::string text_;
};

}  // namespace

// Compared against the value CMake holds, not a literal copied from the header,
// which would compare the header with itself.
TEST(PicoLogging, VersionMatchesTheBuildSystem) {
    EXPECT_EQ(pico_logging::VERSION_MAJOR, PICO_LOGGING_CMAKE_VERSION_MAJOR);
    EXPECT_EQ(pico_logging::VERSION_MINOR, PICO_LOGGING_CMAKE_VERSION_MINOR);
    EXPECT_EQ(pico_logging::VERSION_PATCH, PICO_LOGGING_CMAKE_VERSION_PATCH);
}

// The umbrella alone installs a sink and emits through it.
TEST(PicoLogging, UmbrellaHeaderAloneCanLog) {
    Recorder sink;
    pico_logging::set_sink(&sink);

    PICO_LOG_ERROR << "umbrella";

    pico_logging::set_sink(nullptr);
    EXPECT_NE(sink.text().find("umbrella"), std::string::npos);
}

// Every public sink reaches this file through the umbrella.
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
