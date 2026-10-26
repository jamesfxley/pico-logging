// SPDX-License-Identifier: MIT
// CaptureSink is a shipped fake, so it needs its own tests: a double whose
// behaviour nothing pins is a double that drifts from the seam it stands in
// for, which is the failure this module exists to stop repeating.
#include "pico_logging/testing/capture_sink.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "pico_logging/logging/sinks.hpp"

namespace {

using pico_logging::testing::CaptureSink;

// FR-10
TEST(CaptureSink, RecordsEachLineWholeAndInOrder) {
    CaptureSink sink;
    EXPECT_TRUE(sink.empty());

    sink.write("first\n");
    sink.write("second\n");

    EXPECT_EQ(sink.writes(), 2U);
    ASSERT_EQ(sink.lines().size(), 2U);
    EXPECT_EQ(sink.lines()[0], "first\n");
    EXPECT_EQ(sink.lines()[1], "second\n");
    EXPECT_EQ(sink.str(), "first\nsecond\n");
    EXPECT_EQ(sink.last(), "second\n");
    EXPECT_FALSE(sink.empty());
}

// Framing is the thing lines() exists to assert on and str() cannot: two
// records that arrived as one call would concatenate identically.
// FR-10
TEST(CaptureSink, LinesDistinguishesFramingThatStrCannot) {
    CaptureSink split;
    split.write("a\n");
    split.write("b\n");

    CaptureSink joined;
    joined.write("a\nb\n");

    EXPECT_EQ(split.str(), joined.str());
    EXPECT_NE(split.writes(), joined.writes());
    EXPECT_EQ(split.writes(), 2U);
    EXPECT_EQ(joined.writes(), 1U);
}

// The Sink contract borrows the view for the call. The source is overwritten
// after the call rather than only freed: a sink that stored the view would
// then read the new bytes in any build, not only under a sanitizer.
// FR-9
TEST(CaptureSink, CopiesRatherThanBorrowingTheView) {
    CaptureSink sink;
    const std::string original(64, 'a');
    std::string transient = original;
    sink.write(transient);
    transient.assign(transient.size(), 'z');

    EXPECT_EQ(sink.str(), original);
    EXPECT_EQ(sink.last(), original);
}

// FR-11
TEST(CaptureSink, CountsFlushesSeparately) {
    CaptureSink sink;
    sink.write("x\n");
    sink.flush();
    sink.flush();
    EXPECT_EQ(sink.writes(), 1U);
    EXPECT_EQ(sink.flushes(), 2U);
}

// FR-11
TEST(CaptureSink, ClearDiscardsEverythingIncludingFlushes) {
    CaptureSink sink;
    sink.write("x\n");
    sink.flush();
    sink.clear();

    EXPECT_TRUE(sink.empty());
    EXPECT_EQ(sink.writes(), 0U);
    EXPECT_EQ(sink.flushes(), 0U);
    EXPECT_EQ(sink.str(), "");
    EXPECT_EQ(sink.last(), "");
}

// Empty is a state, not a special case: last() on it must not dereference.
// FR-11
TEST(CaptureSink, LastOnAnEmptySinkIsAnEmptyView) {
    const CaptureSink sink;
    EXPECT_TRUE(sink.last().empty());
    EXPECT_EQ(sink.writes(), 0U);
}

// It is a Sink, and has to be usable as one through the base pointer the
// library installs.
// FR-11
TEST(CaptureSink, WorksThroughTheSinkInterface) {
    CaptureSink sink;
    pico_logging::Sink& seam = sink;
    seam.write("through-the-seam\n");
    seam.flush();
    EXPECT_EQ(sink.str(), "through-the-seam\n");
    EXPECT_EQ(sink.flushes(), 1U);
}

}  // namespace
