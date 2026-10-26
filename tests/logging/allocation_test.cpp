// SPDX-License-Identifier: MIT
// Proves AR-1: the log path allocates nothing.
//
// This is the only test that replaces the global allocation functions, and it
// is its own executable for that reason -- the replacement is program-wide, so
// hosting it alongside tests that allocate freely would make every one of them
// depend on the counter's arming state.
//
// The counter is armed around the statements under test and disarmed
// everywhere else, because GoogleTest itself allocates while reporting.
//
// The counter and the replaced allocation functions are pico_util::testing's,
// which replaces every form: a form left alone keeps the implementation's
// allocator, and a block from it reaching a replaced deallocation function is
// an alloc-dealloc-mismatch.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

#include "pico_logging/logging/logging.hpp"
#include "pico_logging/logging/ring_sink.hpp"
#include "pico_logging/logging/sinks.hpp"
#include "pico_util/testing/allocation_counter.hpp"

// Exactly one translation unit per test executable may include this, and this
// is that unit: the replaceable allocation functions cannot be inline, so a
// second include is a duplicate-symbol link error.
#include "pico_util/testing/allocation_operators.ipp"  // IWYU pragma: keep, it defines the replaced operators

namespace {

using pico_util::testing::AllocationGuard;

/// Sink with fixed storage: a std::string-backed one would allocate and the
/// counter could not tell that apart from the library allocating.
class FixedSink : public pico_logging::Sink {
   public:
    void write(std::string_view text) override {
        const std::size_t to_copy = std::min(text.size(), data_.size() - size_);
        std::memcpy(data_.data() + size_, text.data(), to_copy);
        size_ += to_copy;
        ++writes_;
    }

    [[nodiscard]] std::string_view str() const { return {data_.data(), size_}; }

    [[nodiscard]] int writes() const { return writes_; }

    void clear() {
        size_ = 0;
        writes_ = 0;
    }

   private:
    static constexpr std::size_t CAPACITY = std::size_t{64} * 1024;
    std::array<char, CAPACITY> data_{};
    std::size_t size_ = 0;
    int writes_ = 0;
};

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

}  // namespace

TEST(AllocationTest, TheCounterObservesAnAllocation) {
    const AllocationGuard guard;
    // A string far past any small-string optimisation, so it must reach the
    // heap. Owning the allocation through std::string rather than a raw new
    // keeps this to the behaviour under test and needs no suppression.
    const std::string forced(1024, 'x');
    const std::size_t observed = AllocationGuard::count();
    EXPECT_GE(observed, 1U) << "the counter cannot see allocations, so a zero "
                               "elsewhere in this file proves nothing";
    EXPECT_EQ(forced.size(), 1024U);
}

// AR-1. Every inserter branch, over a burst long enough to exhaust anything a
// lazily-initialised formatter might have cached on the first call.
// AR-1, FR-7
TEST(AllocationTest, TheLogPathAllocatesNothing) {
    FixedSink sink;
    const SinkGuard sink_guard(&sink);
    pico_logging::set_level(pico_logging::Level::TRACE);

    // Warm the path once outside the counter: the point of the measurement is
    // steady-state behaviour, and any one-time cost belongs to start-up.
    PICO_LOG_ERROR << "warm";
    sink.clear();

    constexpr int BURST = 100;
    // A string literal is a char array, so this covers that inserter branch;
    // the mutable-array case belongs to logging_test, which is where FR-4's
    // behaviour rather than its allocation is under test.
    constexpr std::int8_t SMALL_SIGNED = -5;
    constexpr double SINGLE_VALUE = 1.5;
    constexpr double RECURRING = 1.0 / 3.0;
    // Wider than double on this host, so the narrowing check runs too.
    constexpr long double WIDE = 2.5L;
    const std::string_view view = "string_view";
    const char* const pointer = "pointer";

    // No inserter, so the placeholder branch.
    struct Opaque {
        int field = 0;
    };

    const AllocationGuard guard;
    for (int i = 0; i < BURST; ++i) {
        PICO_LOG_ERROR << "literal " << view << ' ' << true << ' '
                       << SMALL_SIGNED << ' ' << static_cast<std::uint64_t>(i)
                       << ' ' << SINGLE_VALUE << ' ' << RECURRING << ' '
                       << static_cast<const char*>(nullptr) << ' ' << pointer
                       << ' ' << nullptr << ' ' << WIDE << ' ' << Opaque{}
                       << " escaped\n\\";
    }
    const std::size_t observed = AllocationGuard::count();

    EXPECT_EQ(observed, 0U);
    EXPECT_EQ(sink.writes(), BURST);
}

// A truncating line takes a different path through the buffer -- the rewind
// and the marker -- and must not allocate either.
// AR-1
TEST(AllocationTest, TruncationAllocatesNothing) {
    FixedSink sink;
    const SinkGuard sink_guard(&sink);

    std::array<char, pico_logging::LOG_BUFFER_CAPACITY * 2> overlong{};
    overlong.fill('x');
    const std::string_view overlong_view(overlong.data(), overlong.size());

    PICO_LOG_ERROR << "warm";

    const AllocationGuard guard;
    PICO_LOG_ERROR << overlong_view << "tail";
    const std::size_t observed = AllocationGuard::count();

    EXPECT_EQ(observed, 0U);
    EXPECT_NE(sink.str().find("..."), std::string_view::npos);
}

// The IRQ-side sink is on the same path and carries the same obligation.
// AR-1
TEST(AllocationTest, RingSinkQueuesAndDrainsWithoutAllocating) {
    FixedSink target;
    std::mutex lock;
    pico_logging::RingSink<4, std::mutex> ring(target, lock);
    const SinkGuard sink_guard(&ring);

    PICO_LOG_ERROR << "warm";
    ring.drain();
    target.clear();

    const AllocationGuard guard;
    PICO_LOG_ERROR << "queued " << 1;
    PICO_LOG_ERROR << "queued " << 2;
    const std::size_t drained = ring.drain();
    const std::size_t observed = AllocationGuard::count();

    EXPECT_EQ(observed, 0U);
    EXPECT_EQ(drained, 2U);
    EXPECT_EQ(target.writes(), 2);
}
