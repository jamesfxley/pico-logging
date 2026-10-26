// SPDX-License-Identifier: MIT
#include "pico_logging/logging/ring_sink.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "pico_logging/testing/capture_sink.hpp"

namespace {

using pico_logging::testing::CaptureSink;

// The host's producer lock. It excludes threads, which is what a host test
// has; a firmware image supplies one that also masks interrupts.
using Ring2 = pico_logging::RingSink<2, std::mutex>;
using Ring4 = pico_logging::RingSink<4, std::mutex>;

// Counts lock() and unlock() and refuses a nested lock(), so a test can see
// that every write() takes the lock exactly once and drain() never does.
class RecordingLock {
   public:
    void lock() {
        EXPECT_FALSE(held_) << "the producer lock was taken while held";
        held_ = true;
        ++locks_;
    }

    void unlock() {
        EXPECT_TRUE(held_) << "the producer lock was released while free";
        held_ = false;
        ++unlocks_;
    }

    [[nodiscard]] int locks() const { return locks_; }

    [[nodiscard]] int unlocks() const { return unlocks_; }

    [[nodiscard]] bool held() const { return held_; }

   private:
    bool held_ = false;
    int locks_ = 0;
    int unlocks_ = 0;
};

// Each concurrent line is "<producer>:<sequence>:" followed by a body that is
// a function of both, and a newline. A line torn between two writes, a slot
// read before its bytes were published, or a slot read again after reuse
// fails to reproduce its own body, or repeats or reorders a sequence number.
constexpr std::size_t CONCURRENT_BODY = 200;

std::string concurrent_line(int producer, int sequence) {
    std::string line =
        std::to_string(producer) + ":" + std::to_string(sequence) + ":";
    for (std::size_t i = 0; i < CONCURRENT_BODY; ++i) {
        line.push_back(
            static_cast<char>('a' + ((static_cast<std::size_t>(producer) * 7U +
                                      static_cast<std::size_t>(sequence) + i) %
                                     26U)));
    }
    line.push_back('\n');
    return line;
}

// A non-negative decimal integer, or nothing if `text` is not one.
std::optional<int> parse_index(std::string_view text) {
    constexpr int RADIX = 10;
    if (text.empty() || text.size() > 9) {
        return std::nullopt;
    }
    int value = 0;
    for (const char digit : text) {
        if (digit < '0' || digit > '9') {
            return std::nullopt;
        }
        value = value * RADIX + (digit - '0');
    }
    return value;
}

// What check_stream found, or nothing when the stream is sound.
using StreamFault = std::optional<std::string>;

// Checks a forwarded stream against what `producers` producers each offered
// `per_producer` lines of: every line is one that was offered, byte for byte;
// each producer's sequence numbers strictly increase, so none is repeated or
// reordered; and forwarded plus dropped equals offered.
StreamFault check_stream(const std::vector<std::string>& lines,
                         std::uint32_t dropped, int producers,
                         int per_producer) {
    std::vector<int> last(static_cast<std::size_t>(producers), -1);
    for (const std::string& line : lines) {
        const std::string_view view(line);
        const std::size_t first = view.find(':');
        const std::size_t second = view.find(':', first + 1);
        if (first == std::string_view::npos ||
            second == std::string_view::npos) {
            return "unparseable line: " + line;
        }
        const std::optional<int> parsed_producer =
            parse_index(view.substr(0, first));
        const std::optional<int> parsed_sequence =
            parse_index(view.substr(first + 1, second - first - 1));
        if (!parsed_producer.has_value() || !parsed_sequence.has_value() ||
            *parsed_producer >= producers || *parsed_sequence >= per_producer) {
            return "unparseable line: " + line;
        }
        const int producer = *parsed_producer;
        const int sequence = *parsed_sequence;
        if (line != concurrent_line(producer, sequence)) {
            return "torn line: " + line;
        }
        int& previous = last[static_cast<std::size_t>(producer)];
        if (sequence <= previous) {
            return "repeated or reordered line: " + line;
        }
        previous = sequence;
    }
    const auto offered = static_cast<std::size_t>(producers) *
                         static_cast<std::size_t>(per_producer);
    if (lines.size() + dropped != offered) {
        return "forwarded " + std::to_string(lines.size()) + " + dropped " +
               std::to_string(dropped) + " != offered " +
               std::to_string(offered);
    }
    return std::nullopt;
}

// Runs `producers` threads, each offering `per_producer` numbered lines, and
// one consumer draining until they finish. Returns the check's verdict.
template <typename Lock>
StreamFault run_concurrently(int producers, int per_producer) {
    CaptureSink target;
    Lock lock;
    pico_logging::RingSink<8, Lock> ring(target, lock);
    std::atomic<int> running = producers;

    std::thread consumer([&ring, &running]() {
        while (running.load(std::memory_order_acquire) > 0) {
            ring.drain();
        }
        ring.drain();
    });
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(producers));
    for (int producer = 0; producer < producers; ++producer) {
        threads.emplace_back([&ring, &running, producer, per_producer]() {
            for (int sequence = 0; sequence < per_producer; ++sequence) {
                ring.write(concurrent_line(producer, sequence));
            }
            running.fetch_sub(1, std::memory_order_release);
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    consumer.join();
    return check_stream(target.lines(), ring.dropped(), producers,
                        per_producer);
}

}  // namespace

// CR-4, FR-2
TEST(RingSinkTest, ForwardsQueuedLinesInOrderOnDrain) {
    CaptureSink target;
    std::mutex lock;
    Ring4 ring(target, lock);

    ring.write("first\n");
    ring.write("second\n");

    // Nothing reaches the target until the consumer asks for it: that is the
    // whole point of putting this in front of a sink an ISR cannot call.
    EXPECT_TRUE(target.lines().empty());
    EXPECT_EQ(ring.size(), 2U);

    EXPECT_EQ(ring.drain(), 2U);
    ASSERT_EQ(target.lines().size(), 2U);
    EXPECT_EQ(target.lines()[0], "first\n");
    EXPECT_EQ(target.lines()[1], "second\n");
    // One write and one flush per line, as the Sink contract requires.
    EXPECT_EQ(target.flushes(), 2U);
    EXPECT_EQ(ring.size(), 0U);
    EXPECT_EQ(ring.dropped(), 0U);
}

// CR-4
TEST(RingSinkTest, DrainOfAnEmptyRingIsANoOp) {
    CaptureSink target;
    std::mutex lock;
    Ring4 ring(target, lock);

    EXPECT_EQ(ring.drain(), 0U);
    EXPECT_TRUE(target.lines().empty());
    EXPECT_EQ(target.flushes(), 0U);
}

// The index wraps at the slot count, and a drained slot becomes reusable.
// AR-4
TEST(RingSinkTest, IndicesWrapAndDrainedSlotsAreReused) {
    CaptureSink target;
    constexpr std::size_t SLOTS = 3;
    std::mutex lock;
    pico_logging::RingSink<SLOTS, std::mutex> ring(target, lock);

    // Several times round the ring, so every slot is written, drained and
    // written again.
    constexpr int ROUNDS = 10;
    for (int round = 0; round < ROUNDS; ++round) {
        ring.write("line\n");
        EXPECT_EQ(ring.drain(), 1U);
    }

    EXPECT_EQ(target.lines().size(), static_cast<std::size_t>(ROUNDS));
    EXPECT_EQ(ring.dropped(), 0U);
    EXPECT_EQ(ring.size(), 0U);
}

// A full ring drops whole lines. It never forwards a partial one, which is
// what lets the target keep assuming one write is one line.
// BR-5, OR-6, AR-4
TEST(RingSinkTest, AFullRingDropsWholeLinesAndCountsThem) {
    CaptureSink target;
    constexpr std::size_t SLOTS = 2;
    std::mutex lock;
    pico_logging::RingSink<SLOTS, std::mutex> ring(target, lock);

    ring.write("kept 1\n");
    ring.write("kept 2\n");
    EXPECT_EQ(ring.size(), SLOTS);

    ring.write("dropped 1\n");
    ring.write("dropped 2\n");
    EXPECT_EQ(ring.dropped(), 2U);
    EXPECT_EQ(ring.size(), SLOTS);

    EXPECT_EQ(ring.drain(), SLOTS);
    ASSERT_EQ(target.lines().size(), SLOTS);
    EXPECT_EQ(target.lines()[0], "kept 1\n");
    EXPECT_EQ(target.lines()[1], "kept 2\n");

    // Space is available again once drained.
    ring.write("kept 3\n");
    EXPECT_EQ(ring.drain(), 1U);
    EXPECT_EQ(target.lines().back(), "kept 3\n");
    EXPECT_EQ(ring.dropped(), 2U);
}

// A line longer than a slot is dropped rather than truncated: forwarding a
// prefix would hand the target something that is not a line.
// BR-5, OR-6
TEST(RingSinkTest, AnOversizedLineIsDroppedRatherThanTruncated) {
    CaptureSink target;
    std::mutex lock;
    Ring2 ring(target, lock);

    const std::string oversized(pico_logging::LOG_LINE_SIZE + 1, 'x');
    ring.write(oversized);

    EXPECT_EQ(ring.dropped(), 1U);
    EXPECT_EQ(ring.size(), 0U);
    EXPECT_EQ(ring.drain(), 0U);
    EXPECT_TRUE(target.lines().empty());

    // Exactly a slot's worth still fits.
    const std::string exact(pico_logging::LOG_LINE_SIZE, 'y');
    ring.write(exact);
    EXPECT_EQ(ring.drain(), 1U);
    ASSERT_EQ(target.lines().size(), 1U);
    EXPECT_EQ(target.lines()[0], exact);
}

// A whole line goes in and the same whole line comes out, byte for byte,
// including a line at exactly the capacity of a slot.
// BR-5
TEST(RingSinkTest, LinesRoundTripUnchanged) {
    CaptureSink target;
    std::mutex lock;
    Ring4 ring(target, lock);

    const std::string with_embedded_newlines = "a\nb\nc\n";
    ring.write(with_embedded_newlines);
    ring.write("");

    EXPECT_EQ(ring.drain(), 2U);
    ASSERT_EQ(target.lines().size(), 2U);
    EXPECT_EQ(target.lines()[0], with_embedded_newlines);
    EXPECT_TRUE(target.lines()[1].empty());
}

// Every write() takes the producer lock exactly once -- on the drop paths as
// well as the accepting one -- and drain() takes it never.
// CR-4
TEST(RingSinkTest, EveryWriteTakesTheProducerLockOnceAndDrainNever) {
    CaptureSink target;
    RecordingLock lock;
    pico_logging::RingSink<1, RecordingLock> ring(target, lock);

    ring.write("kept\n");
    ring.write("dropped, the ring is full\n");
    ring.write(std::string(pico_logging::LOG_LINE_SIZE + 1, 'x'));
    EXPECT_EQ(lock.locks(), 3);
    EXPECT_EQ(lock.unlocks(), 3);
    EXPECT_FALSE(lock.held());
    EXPECT_EQ(ring.dropped(), 2U);

    EXPECT_EQ(ring.drain(), 1U);
    EXPECT_EQ(lock.locks(), 3);
}

// The stream check the concurrent test relies on, shown to fail on each fault
// it exists to find. A check that has only ever passed establishes nothing.
// CR-4
TEST(RingSinkTest, TheStreamCheckRejectsEachFault) {
    const std::string first = concurrent_line(0, 0);
    const std::string second = concurrent_line(0, 1);

    EXPECT_FALSE(check_stream({first, second}, 0, 1, 2).has_value());

    std::string torn = second;
    torn[torn.size() / 2] = '#';
    EXPECT_TRUE(check_stream({first, torn}, 0, 1, 2).has_value());
    EXPECT_TRUE(check_stream({first, first}, 0, 1, 2).has_value());
    EXPECT_TRUE(check_stream({second, first}, 0, 1, 2).has_value());
    EXPECT_TRUE(check_stream({first}, 0, 1, 2).has_value());
    EXPECT_TRUE(check_stream({first, second}, 1, 1, 2).has_value());
    EXPECT_TRUE(check_stream({"garbage\n"}, 1, 1, 2).has_value());
}

// Several producer threads and one consumer, serialised by the lock the
// class requires. Every line comes out whole, once, in its producer's order,
// and whatever did not come out was counted.
// CR-4, OR-6
TEST(RingSinkTest, SurvivesConcurrentProducersAndAConsumer) {
    constexpr int PRODUCERS = 3;
    constexpr int PER_PRODUCER = 20000;
    const StreamFault fault =
        run_concurrently<std::mutex>(PRODUCERS, PER_PRODUCER);
    if (fault.has_value()) {
        FAIL() << *fault;
    }
}
