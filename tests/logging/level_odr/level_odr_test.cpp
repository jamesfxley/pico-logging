// SPDX-License-Identifier: MIT
// OR-2. Two translation units of one binary, compiled at different ceilings.
//
// Were LOGGING_GLOBAL_LEVEL to feed an `inline constexpr` variable or an
// `inline` function, the two units would hold two definitions of one entity --
// an ODR violation the linker resolves by keeping one and discarding the other,
// so which unit logs would depend on link order and optimisation level. The
// ceiling reaches no linked entity: it is a macro, applied by `if constexpr`
// inside the PICO_LOG_* macro, so each translation unit decides alone and there
// is nothing for the linker to choose between. This test links the two units
// and asserts each behaves as it was compiled.

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "level_units.hpp"
#include "pico_logging/logging/logging.hpp"
#include "pico_logging/logging/sinks.hpp"

namespace {

class CaptureSink : public pico_logging::Sink {
   public:
    void write(std::string_view text) override { buffer_.append(text); }

    [[nodiscard]] const std::string& str() const { return buffer_; }

    void clear() { buffer_.clear(); }

   private:
    std::string buffer_;
};

class LevelOdrTest : public ::testing::Test {
   protected:
    void SetUp() override {
        original_level_ = pico_logging::get_level();
        pico_logging::set_sink(&sink_);
        // As permissive as the runtime filter goes, so anything that does not
        // appear was stopped by the compile-time ceiling and nothing else.
        pico_logging::set_level(pico_logging::Level::TRACE);
    }

    void TearDown() override {
        pico_logging::set_level(original_level_);
        pico_logging::set_sink(nullptr);
    }

    [[nodiscard]] CaptureSink& sink() { return sink_; }

   private:
    CaptureSink sink_;
    pico_logging::Level original_level_ = pico_logging::Level::INFO;
};

}  // namespace

TEST_F(LevelOdrTest, EachUnitLogsAtTheLevelItWasCompiledWith) {
    level_odr::log_debug_from_verbose_unit();
    level_odr::log_debug_from_quiet_unit();

    const std::string& out = sink().str();
    // Compiled at 4: DBG passes its ceiling.
    EXPECT_NE(out.find(level_odr::VERBOSE_MESSAGE), std::string::npos) << out;
    // Compiled at 1: DBG is above its ceiling, so the statement is not there to
    // run. Finding this string is the ODR bug returning.
    EXPECT_EQ(out.find(level_odr::QUIET_MESSAGE), std::string::npos) << out;
}

TEST_F(LevelOdrTest, ALevelBelowBothCeilingsFiresInBothUnits) {
    level_odr::log_error_from_verbose_unit();
    level_odr::log_error_from_quiet_unit();

    const std::string& out = sink().str();
    EXPECT_NE(out.find("verbose error"), std::string::npos) << out;
    EXPECT_NE(out.find("quiet error"), std::string::npos) << out;
}

// The runtime level is one value for the whole image, whatever ceiling each
// unit was compiled at, and it is not capped by any of them (OR-3).
TEST_F(LevelOdrTest, TheRuntimeLevelIsSharedAndUncapped) {
    pico_logging::set_level(pico_logging::Level::TRACE);
    EXPECT_EQ(pico_logging::get_level(), pico_logging::Level::TRACE);

    pico_logging::set_level(pico_logging::Level::ERR);
    level_odr::log_debug_from_verbose_unit();
    // Below its ceiling, but above the runtime level: nothing is emitted.
    EXPECT_TRUE(sink().str().empty()) << sink().str();

    level_odr::log_error_from_verbose_unit();
    EXPECT_NE(sink().str().find("verbose error"), std::string::npos);
}
