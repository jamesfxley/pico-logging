// SPDX-License-Identifier: MIT
#include "pico_logging/logging/logging.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <regex>
#include <string>
#include <string_view>

#include "pico_logging/logging/sinks.hpp"
#include "pico_logging/testing/capture_sink.hpp"

namespace {

using pico_logging::testing::CaptureSink;

// Installs a sink for the duration of a scope, restoring the default on exit.
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

// Restores the runtime level on scope exit, so a moved level cannot leak into
// the next test.
struct LevelGuard {
    LevelGuard() : original_(pico_logging::get_level()) {}

    ~LevelGuard() { pico_logging::set_level(original_); }

    LevelGuard(const LevelGuard&) = delete;
    LevelGuard& operator=(const LevelGuard&) = delete;
    LevelGuard(LevelGuard&&) = delete;
    LevelGuard& operator=(LevelGuard&&) = delete;

   private:
    pico_logging::Level original_;
};

// Counts operand evaluations. A function-local static and not a namespace
// variable, which would need a suppression.
int& evaluation_count() {
    static int count = 0;
    return count;
}

int counted_operand() {
    ++evaluation_count();
    return evaluation_count();
}

}  // namespace

// FR-1
TEST(LoggingTest, LabelAndColorMatchLevels) {
    using ::pico_logging::Level;
    EXPECT_STREQ(::pico_logging::label_for(Level::ERR), "ERROR");
    EXPECT_STREQ(::pico_logging::label_for(Level::WARN), "WARN");
    EXPECT_STREQ(::pico_logging::label_for(Level::INFO), "INFO");
    EXPECT_STREQ(::pico_logging::label_for(Level::DBG), "DEBUG");
    EXPECT_STREQ(::pico_logging::label_for(Level::TRACE), "TRACE");
    EXPECT_STREQ(::pico_logging::label_for(Level::NONE), "NONE");

    EXPECT_STREQ(::pico_logging::color_for(Level::ERR),
                 ::pico_logging::COLOR_RED);
    EXPECT_STREQ(::pico_logging::color_for(Level::WARN),
                 ::pico_logging::COLOR_YELLOW);
    EXPECT_STREQ(::pico_logging::color_for(Level::INFO),
                 ::pico_logging::COLOR_GREEN);
    EXPECT_STREQ(::pico_logging::color_for(Level::DBG),
                 ::pico_logging::COLOR_CYAN);
    EXPECT_STREQ(::pico_logging::color_for(Level::TRACE),
                 ::pico_logging::COLOR_BLUE);
    EXPECT_STREQ(::pico_logging::color_for(Level::NONE),
                 ::pico_logging::COLOR_RESET);
}

// FR-6
TEST(LoggingTest, FilenameOnlyStripsDirectorySeparators) {
    EXPECT_STREQ(::pico_logging::filename_only(""), "");
    EXPECT_STREQ(::pico_logging::filename_only(nullptr), "");
    EXPECT_STREQ(::pico_logging::filename_only("main.cpp"), "main.cpp");
    EXPECT_STREQ(::pico_logging::filename_only("path/to/file.cpp"), "file.cpp");
    EXPECT_STREQ(::pico_logging::filename_only("path\\to\\file.cpp"),
                 "file.cpp");
    EXPECT_STREQ(::pico_logging::filename_only("C:\\src\\lib/deep\\file.cpp"),
                 "file.cpp");
    EXPECT_STREQ(::pico_logging::filename_only("C:\\src/lib/file.cpp"),
                 "file.cpp");
    // A path that is nothing but a separator has an empty trailing component.
    EXPECT_STREQ(::pico_logging::filename_only("/"), "");
}

// Reading to the array's full extent would emit the trailing NULs.
// FR-4
TEST(LoggingTest, CharArrayInserterStopsAtTheTerminator) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    // A fixed scratch buffer filled well short of its extent. A C array is the
    // type under test, and std::array selects a different branch, so the
    // remedy for modernize-avoid-c-arrays would delete the test.
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    const char address[16] = "192.168.0.42";
    PICO_LOG_WARN << "ip=[" << address << "]";

    const std::string& out = sink.str();
    EXPECT_NE(out.find("ip=[192.168.0.42]"), std::string::npos) << out;
    EXPECT_EQ(out.find('\0'), std::string::npos)
        << "NUL bytes reached the sink";
}

// FR-4
TEST(LoggingTest, CharArrayInserterHandlesTheEdgeShapes) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    // No terminator in the extent: the whole array renders and nothing past it
    // is read. A C array is the type under test, as in
    // CharArrayInserterStopsAtTheTerminator.
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    const char unterminated[4] = {'a', 'b', 'c', 'd'};
    PICO_LOG_WARN << "[" << unterminated << "]";
    EXPECT_NE(sink.str().find("[abcd]"), std::string::npos) << sink.str();

    sink.clear();
    // A C array is the type under test, as above.
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    const char empty[8] = "";
    PICO_LOG_WARN << "[" << empty << "]";
    EXPECT_NE(sink.str().find("[]"), std::string::npos) << sink.str();

    sink.clear();
    PICO_LOG_WARN << "[" << "literal" << "]";
    EXPECT_NE(sink.str().find("[literal]"), std::string::npos) << sink.str();
}

// OR-2
TEST(LoggingTest, IsEnabledReflectsTheRuntimeLevelAlone) {
    using ::pico_logging::Level;
    const LevelGuard guard;

    ::pico_logging::set_level(Level::WARN);
    EXPECT_TRUE(::pico_logging::is_enabled(Level::ERR));
    EXPECT_TRUE(::pico_logging::is_enabled(Level::WARN));
    EXPECT_FALSE(::pico_logging::is_enabled(Level::INFO));

    // DBG is above this TU's ceiling of 2, yet is_enabled() answers true.
    ::pico_logging::set_level(Level::DBG);
    EXPECT_TRUE(::pico_logging::is_enabled(Level::DBG));
}

// PICO_LOG_ENABLED_<LEVEL> is the combined check, and the only one that sees
// this TU's ceiling. OR-1
TEST(LoggingTest, LogEnabledCombinesBothFilters) {
    using ::pico_logging::Level;
    const LevelGuard guard;

    ::pico_logging::set_level(Level::TRACE);
    EXPECT_TRUE(PICO_LOG_ENABLED_ERR);
    EXPECT_TRUE(PICO_LOG_ENABLED_WARN);
    // The runtime level is as permissive as it goes; the ceiling still wins.
    EXPECT_FALSE(PICO_LOG_ENABLED_INFO);
    EXPECT_FALSE(PICO_LOG_ENABLED_TRACE);

    ::pico_logging::set_level(Level::ERR);
    EXPECT_TRUE(PICO_LOG_ENABLED_ERR);
    EXPECT_FALSE(PICO_LOG_ENABLED_WARN);
}

// Control bytes render as escapes, a backslash doubles, UTF-8 passes through.
// FR-1
TEST(LoggingTest, ControlCharactersInTextAreEscaped) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    const std::string_view forged =
        "x\n2026-01-01 00:00:00 [ERROR] auth.cpp:1: ok\r\t\x1b\x7f";
    // A string literal is a char array, so "c\nd" takes the array branch.
    const char* const pointer = "p\nq";
    PICO_LOG_WARN << forged << '\n'
                  << "end" << pointer << "c\nd"
                  << " C:\\new 25\xc2\xb0"
                     "C";
    const std::string& output = sink.str();
    EXPECT_EQ(output.find('\n'), output.size() - 1)
        << "a message spanning lines: " << output;
    EXPECT_NE(
        output.find(
            "x\\n2026-01-01 00:00:00 [ERROR] auth.cpp:1: ok\\r\\t\\x1b\\x7f"
            "\\nendp\\nqc\\nd C:\\\\new 25\xc2\xb0"
            "C\n"),
        std::string::npos)
        << "actual: " << output;
}

// FR-1
TEST(LoggingTest, LogLineWritesWhenEnabled) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    constexpr int TEST_VALUE = 42;

    PICO_LOG_WARN << "warning" << TEST_VALUE;
    const std::string& enabled_output = sink.str();
    EXPECT_NE(enabled_output.find("WARN"), std::string::npos);
    // The whole location field, because the timestamp carries colons of its
    // own and nothing shorter shows the location is there.
    const std::regex location(R"(\] logging_test\.cpp:\d+: warning42\n$)");
    EXPECT_TRUE(std::regex_search(enabled_output, location))
        << "actual: " << enabled_output;

    sink.clear();
    PICO_LOG_INFO << "filtered";  // above the compile-time ceiling (WARN)
    EXPECT_TRUE(sink.str().empty());
}

// FR-1, FR-8
TEST(LoggingTest, HeaderStartsWithFormattedTimestamp) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    PICO_LOG_WARN << "x";
    // Regression for the too-small strftime buffer: the seconds field must be
    // a complete "YYYY-MM-DD HH:MM:SS.mmm", not garbage or truncated.
    const std::regex timestamp_prefix(
        R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} \[)");
    EXPECT_TRUE(std::regex_search(sink.str(), timestamp_prefix))
        << "actual: " << sink.str();
}

// The too-small-buffer arm of format_timestamp is the only one a host test can
// drive; the FR-8 row of the traceability matrix in
// docs/technical-specification.md says why.
// FR-8
TEST(LoggingTest, ATooSmallBufferLeavesTheTimestampEmpty) {
    std::array<char, pico_logging::TIMESTAMP_BUFFER_SIZE - 1> buffer{};

    const std::string_view rendered =
        pico_logging::detail::format_timestamp(buffer);

    EXPECT_TRUE(rendered.empty()) << "rendered: " << rendered;
}

// The allocation suite inserts a null char* and asserts only that nothing
// allocates. What a consumer greps for is the text.
// FR-7
TEST(LoggingTest, ANullCharPointerRendersAsNull) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    const char* absent = nullptr;
    PICO_LOG_WARN << absent;

    EXPECT_NE(sink.str().find("(null)"), std::string::npos)
        << "actual: " << sink.str();
}

// FR-3, FR-5
TEST(LoggingTest, FormatsAllIntegerWidthsAndScalars) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    // int8_t is the lwIP err_t type. The fixed-width aliases name different
    // fundamental types per platform, so every fundamental width is inserted
    // below as well.
    constexpr std::int8_t I8 = -5;
    constexpr std::int32_t I32 = -2000000000;
    constexpr unsigned int UINT = 7U;
    constexpr std::uint64_t U64 = UINT64_MAX;

    PICO_LOG_WARN << "i8=" << I8 << " i32=" << I32 << " u=" << UINT
                  << " u64=" << U64 << " b=" << true << " c=" << 'Q';

    const std::string& out = sink.str();
    EXPECT_EQ(out.find("<?>"), std::string::npos) << out;
    EXPECT_NE(out.find("i8=-5"), std::string::npos) << out;
    EXPECT_NE(out.find("i32=-2000000000"), std::string::npos) << out;
    EXPECT_NE(out.find("u=7"), std::string::npos) << out;
    EXPECT_NE(out.find("u64=18446744073709551615"), std::string::npos) << out;
    EXPECT_NE(out.find("b=true"), std::string::npos) << out;
    EXPECT_NE(out.find("c=Q"), std::string::npos) << out;
}

// Both compiled integer widths retain their complete value range.
// FR-3
TEST(LoggingTest, FormatsIntegerExtrema) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    PICO_LOG_WARN << "min=" << std::numeric_limits<std::int64_t>::min()
                  << " max=" << std::numeric_limits<std::int64_t>::max()
                  << " unsigned=" << std::numeric_limits<std::uint64_t>::max();

    const std::string& out = sink.str();
    EXPECT_NE(out.find("min=-9223372036854775808"), std::string::npos) << out;
    EXPECT_NE(out.find("max=9223372036854775807"), std::string::npos) << out;
    EXPECT_NE(out.find("unsigned=18446744073709551615"), std::string::npos)
        << out;
}

// Every fundamental integral spelling, both signednesses, and the string and
// floating-point branches, one statement each so a failure names the type.
// FR-3
TEST(LoggingTest, EveryFundamentalIntegralTypeRendersItsValue) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    const auto rendered = [&sink](const auto& value) {
        sink.clear();
        PICO_LOG_WARN << "[" << value << "]";
        const std::string& out = sink.str();
        const std::size_t open = out.rfind('[');
        const std::size_t close = out.rfind(']');
        return open != std::string::npos && close != std::string::npos
                   ? out.substr(open + 1, close - open - 1)
                   : out;
    };

    // Each value comes from a literal of its type or from the fixed-width alias
    // naming it on every target, so the fundamental type is exact.
    constexpr std::int16_t SHORT = -300;
    constexpr auto LONG = -70000L;
    constexpr auto LONG_LONG = -5000000000LL;
    constexpr std::uint16_t USHORT = 65535U;
    constexpr auto ULONG = 4000000000UL;
    constexpr auto ULONG_LONG = 10000000000ULL;
    constexpr std::int8_t SCHAR = -7;
    constexpr std::uint8_t UCHAR = 200U;
    constexpr float FLOAT = 0.5F;
    constexpr double DOUBLE = -2.25;

    EXPECT_EQ(rendered(SHORT), "-300");
    EXPECT_EQ(rendered(LONG), "-70000");
    EXPECT_EQ(rendered(LONG_LONG), "-5000000000");
    EXPECT_EQ(rendered(USHORT), "65535");
    EXPECT_EQ(rendered(ULONG), "4000000000");
    EXPECT_EQ(rendered(ULONG_LONG), "10000000000");
    EXPECT_EQ(rendered(SCHAR), "-7");
    EXPECT_EQ(rendered(UCHAR), "200");
    EXPECT_EQ(rendered(FLOAT), "0.500000");
    EXPECT_EQ(rendered(DOUBLE), "-2.250000");
    EXPECT_EQ(rendered(std::string_view("view")), "view");
    EXPECT_EQ(rendered(std::string("owned")), "owned");
    // The character types other than char are integral, so they render as
    // their code value by the same trait (FR-3).
    EXPECT_EQ(rendered(u8'A'), "65");
    EXPECT_EQ(rendered(u'B'), "66");
    EXPECT_EQ(rendered(U'C'), "67");
    EXPECT_EQ(rendered(L'D'), "68");
}

// nullptr converts to std::string_view in C++20, and a string_view built from
// it reads address zero. It renders as a null char* does.
// FR-7
TEST(LoggingTest, NullptrRendersAsNull) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    PICO_LOG_WARN << "[" << nullptr << "]";

    EXPECT_NE(sink.str().find("[(null)]"), std::string::npos) << sink.str();
}

// A type with no inserter renders as a placeholder and still compiles.
// FR-3
TEST(LoggingTest, ATypeWithNoInserterRendersAsAPlaceholder) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    struct Opaque {
        int field = 0;
    };

    PICO_LOG_WARN << "[" << Opaque{} << "]";

    EXPECT_NE(sink.str().find("[<?>]"), std::string::npos) << sink.str();
}

// BR-1
TEST(LoggingTest, TruncatesOverlongMessageWithSingleMarker) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    constexpr size_t HUGE_LEN = 1000;
    const std::string huge(HUGE_LEN, 'a');
    PICO_LOG_WARN << huge << "TAIL";

    const std::string& out = sink.str();
    // The marker appears exactly once; "TAIL" (written after truncation) is
    // dropped; the whole line stays bounded by the fixed buffer + header.
    const size_t first = out.find("...");
    ASSERT_NE(first, std::string::npos);
    EXPECT_EQ(out.find("...", first + 1), std::string::npos);
    EXPECT_EQ(out.find("TAIL"), std::string::npos);
    EXPECT_LT(out.size(), pico_logging::LOG_HEADER_SIZE +
                              pico_logging::LOG_BUFFER_CAPACITY + 2);
}

// The Sink contract: one write() per line, then one flush().
// FR-2, FR-5
TEST(LoggingTest, EachLineIsExactlyOneWriteAndOneFlush) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    constexpr double FRACTIONAL = 3.5;
    PICO_LOG_WARN << "one " << 2 << ' ' << FRACTIONAL;

    EXPECT_EQ(sink.writes(), 1U);
    EXPECT_EQ(sink.flushes(), 1U);
    // That single call carried the whole line, newline included.
    EXPECT_EQ(sink.last(), sink.str());
    EXPECT_NE(sink.last().find("WARN"), std::string::npos);
    EXPECT_NE(sink.last().find("one 2 3.5"), std::string::npos);
    ASSERT_FALSE(sink.last().empty());
    EXPECT_EQ(sink.last().back(), '\n');

    sink.clear();
    PICO_LOG_WARN << "second";
    PICO_LOG_WARN << "third";
    EXPECT_EQ(sink.writes(), 2U);
    EXPECT_EQ(sink.flushes(), 2U);
}

// Boundary triple around the message buffer: one below, exactly at, one above.
// BR-3
TEST(LoggingTest, TruncatesOnlyBeyondTheExactCapacity) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    constexpr size_t CAPACITY = pico_logging::LOG_BUFFER_CAPACITY;

    const std::string just_under(CAPACITY - 1, 'a');
    PICO_LOG_WARN << just_under;
    EXPECT_EQ(sink.str().find("..."), std::string::npos) << "truncated early";
    EXPECT_NE(sink.str().find(just_under), std::string::npos);

    sink.clear();
    const std::string exact(CAPACITY, 'b');
    PICO_LOG_WARN << exact;
    EXPECT_EQ(sink.str().find("..."), std::string::npos)
        << "a message of exactly the capacity must not truncate";
    EXPECT_NE(sink.str().find(exact), std::string::npos);

    sink.clear();
    const std::string just_over(CAPACITY + 1, 'c');
    PICO_LOG_WARN << just_over;
    EXPECT_NE(sink.str().find("..."), std::string::npos)
        << "one byte over must truncate";
}

// The marker must not itself be truncated: with two bytes left, an append()
// that copied whatever fits would end the line in "..".
// BR-2
TEST(LoggingTest, TheTruncationMarkerAlwaysLandsWhole) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    constexpr size_t CAPACITY = pico_logging::LOG_BUFFER_CAPACITY;
    constexpr size_t MARKER_LENGTH = 3;

    // Walk the last few bytes of the buffer: every one of these leaves too
    // little room to reserve the marker up front.
    for (size_t remaining = 0; remaining <= MARKER_LENGTH + 1; ++remaining) {
        sink.clear();
        const std::string filler(CAPACITY - remaining, 'x');
        PICO_LOG_WARN << filler << "overflowing tail";

        const std::string& out = sink.str();
        ASSERT_NE(out.find("..."), std::string::npos)
            << "remaining=" << remaining << " produced: " << out;
        // The line ends with the whole marker, before the newline.
        ASSERT_FALSE(out.empty());
        EXPECT_EQ(out.substr(out.size() - MARKER_LENGTH - 1),
                  std::string("...\n"))
            << "remaining=" << remaining << " produced: " << out;
    }

    // The placeholder for a type with no inserter is text like any other, and
    // is cut the same way.
    struct Opaque {
        int field = 0;
    };

    for (size_t remaining = 0; remaining <= MARKER_LENGTH + 1; ++remaining) {
        sink.clear();
        const std::string filler(CAPACITY - remaining, 'x');
        PICO_LOG_WARN << filler << Opaque{};

        const std::string& out = sink.str();
        ASSERT_FALSE(out.empty());
        const bool whole = out.find("<?>") != std::string::npos;
        const bool cut = out.size() > MARKER_LENGTH &&
                         out.substr(out.size() - MARKER_LENGTH - 1) == "...\n";
        EXPECT_TRUE(whole || cut)
            << "remaining=" << remaining << " produced: " << out;
    }
}

// A cut never lands inside an escape: a partial `\x0` or a lone `\` would read
// as text that spells one. Every rewind position is walked for both escape
// sizes.
// BR-2, FR-1
TEST(LoggingTest, TheCutNeverSplitsAnEscape) {
    CaptureSink sink;
    const SinkGuard guard(&sink);
    constexpr size_t CAPACITY = pico_logging::LOG_BUFFER_CAPACITY;
    constexpr std::string_view MARKER = "...\n";

    for (const std::string_view escaped : {"\n", "\x01"}) {
        const std::string rendered = escaped == "\n" ? "\\n" : "\\x01";
        for (size_t before = 0; before <= 8; ++before) {
            sink.clear();
            const std::string filler(CAPACITY - before, 'a');
            PICO_LOG_WARN << filler << escaped << "overflowing rest";
            const std::string& out = sink.str();
            ASSERT_GE(out.size(), MARKER.size());
            ASSERT_EQ(out.substr(out.size() - MARKER.size()), MARKER) << out;
            const std::string body = out.substr(0, out.size() - MARKER.size());
            const std::string kept = body.substr(body.find_last_of('a') + 1);
            // Nothing of the escape, or all of it and then what followed.
            EXPECT_TRUE(kept.empty() || kept.starts_with(rendered))
                << "before=" << before << " kept '" << kept << "'";
        }
    }
}

// A rewind is at most three bytes and an escape at least two, so with two
// escapes side by side the cut can reach the second-last as well as the last.
// Every ordered pair of two- and four-byte escapes is walked across every
// rewind position.
// BR-2, FR-1
TEST(LoggingTest, TheCutNeverSplitsAdjacentEscapes) {
    CaptureSink sink;
    const SinkGuard guard(&sink);
    constexpr size_t CAPACITY = pico_logging::LOG_BUFFER_CAPACITY;
    constexpr std::string_view MARKER = "...\n";

    struct Escape {
        std::string_view raw;
        std::string_view rendered;
    };

    const std::array<Escape, 3> escapes{{{.raw = "\n", .rendered = "\\n"},
                                         {.raw = "\x01", .rendered = "\\x01"},
                                         {.raw = "\\", .rendered = "\\\\"}}};

    for (const Escape& first : escapes) {
        for (const Escape& second : escapes) {
            const std::string both =
                std::string(first.rendered) + std::string(second.rendered);
            for (size_t before = 0; before <= 10; ++before) {
                sink.clear();
                const std::string filler(CAPACITY - before, 'a');
                PICO_LOG_WARN << filler << first.raw << second.raw
                              << "overflowing rest";
                const std::string& out = sink.str();
                ASSERT_GE(out.size(), MARKER.size());
                ASSERT_EQ(out.substr(out.size() - MARKER.size()), MARKER);
                const std::string body =
                    out.substr(0, out.size() - MARKER.size());
                const std::string kept =
                    body.substr(body.find_last_of('a') + 1);
                // Nothing, the first escape whole, or both and what followed.
                EXPECT_TRUE(kept.empty() || kept == first.rendered ||
                            kept.starts_with(both))
                    << "before=" << before << " kept '" << kept << "'";
            }
        }
    }
}

// OR-5, CR-1, CR-2
TEST(LoggingTest, CustomSinkReceivesOutputAndDefaultRestored) {
    CaptureSink sink;
    {
        const SinkGuard guard(&sink);
        EXPECT_EQ(pico_logging::get_sink(), &sink);
        PICO_LOG_WARN << "routed";
        EXPECT_NE(sink.str().find("routed"), std::string::npos);
    }
    // set_sink(nullptr) on scope exit restores the built-in default sink.
    EXPECT_NE(pico_logging::get_sink(), &sink);
    EXPECT_NE(pico_logging::get_sink(), nullptr);
}

// set_level() stores what it is given and does not clamp against the ceiling
// compiled into sinks.cpp.
// OR-3, CR-1
TEST(LoggingTest, SetLevelStoresWhatItIsGiven) {
    using pico_logging::Level;
    const LevelGuard guard;

    pico_logging::set_level(Level::TRACE);
    EXPECT_EQ(pico_logging::get_level(), Level::TRACE);

    pico_logging::set_level(Level::ERR);
    EXPECT_EQ(pico_logging::get_level(), Level::ERR);

    pico_logging::set_level(Level::NONE);
    EXPECT_EQ(pico_logging::get_level(), Level::NONE);
}

// OR-1
TEST(LoggingTest, CompileTimeCeilingDiscardsOperandsAndRuntimeFilterDoesNot) {
    using pico_logging::Level;
    CaptureSink sink;
    const SinkGuard sink_guard(&sink);
    const LevelGuard level_guard;

    pico_logging::set_level(Level::TRACE);

    // INFO is above this TU's ceiling of 2, so the whole statement, operand
    // included, is discarded by `if constexpr` in the macro.
    evaluation_count() = 0;
    PICO_LOG_INFO << counted_operand();
    EXPECT_EQ(evaluation_count(), 0);
    EXPECT_TRUE(sink.str().empty());

    // WARN passes the ceiling but not the runtime level, so the LogLine is
    // constructed and each `<<` is a no-op, and the operand is still evaluated.
    pico_logging::set_level(Level::ERR);
    evaluation_count() = 0;
    PICO_LOG_WARN << counted_operand();
    EXPECT_EQ(evaluation_count(), 1);
    EXPECT_TRUE(sink.str().empty());
}

// The macro's discarded branch is the empty one, so a trailing `else` at the
// call site binds to the caller's own `if` rather than to the macro's.
// OR-7
TEST(LoggingTest, AFilteredStatementDoesNotCaptureATrailingElse) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    // readability-braces-around-statements is suppressed for this region
    // because the unbraced shape is the property under test: braces
    // disambiguate the `else`, so a braced version passes whatever the macro
    // does.
    // NOLINTBEGIN(readability-braces-around-statements)
    bool took_else = false;
    if (sink.str().empty())
        PICO_LOG_INFO << "above the ceiling, discarded";
    else
        took_else = true;
    EXPECT_FALSE(took_else);

    took_else = false;
    if (!sink.str().empty())
        PICO_LOG_WARN << "below the ceiling, emitted";
    else
        took_else = true;
    // Closes the region opened above; see the reason there.
    // NOLINTEND(readability-braces-around-statements)
    EXPECT_TRUE(took_else);
}

// Last in the file: #line renames the file for every line after it.
// BR-4
TEST(LoggingTest, AnOverlongFileNameIsCutFromTheFrontAndTheLineSurvives) {
    CaptureSink sink;
    const SinkGuard guard(&sink);

    // clang-format off
#line 1000 "/deep/dir/ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff_tail.cpp"
    PICO_LOG_WARN << "MESSAGE";
    // clang-format on

    const std::string& out = sink.str();
    const std::regex cut(R"(\] \.\.\.f+_tail\.cpp:1000: MESSAGE\n$)");
    EXPECT_TRUE(std::regex_search(out, cut)) << "actual: " << out;
    // The header stayed inside its reservation.
    EXPECT_LE(out.size(), pico_logging::LOG_HEADER_SIZE +
                              std::string_view("MESSAGE\n").size());
}
