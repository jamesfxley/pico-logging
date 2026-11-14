// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "pico_logging/logging/logging.hpp"
#include "pico_logging/logging/sinks.hpp"
#include "pico_util/containers/ring_buffer.hpp"

/// \file ring_sink.hpp
/// A sink an interrupt handler can write to: the producer copies its line into
/// a slot and the main loop calls drain() to forward it. It removes std::fwrite
/// from the handler (CR-3) and nothing more: the clock read, localtime_r and
/// strftime still run in the producer's context (CR-5).

namespace pico_logging {

/// Bytes in the longest line a LogLine can emit, and in a RingSink slot's data.
/// Derived from the logging buffers, so a slot always holds a full line.
inline constexpr std::size_t LOG_LINE_SIZE =
    LOG_HEADER_SIZE + LOG_BUFFER_CAPACITY + 1;

/// A type usable as RingSink's producer lock: BasicLockable.
/// docs/usage-guide.md has a sample adapter over the SDK's critical_section.
template <typename L>
concept RingSinkLock = requires(L& lock) {
    lock.lock();
    lock.unlock();
};

/// Fixed-capacity queue of whole log lines: any number of producer contexts
/// write, one consumer drains. Allocates nothing.
///
/// `Lock` must exclude every producer context from every other, and is held
/// across one line's copy into a slot and nothing else. A lock whose holder an
/// interrupt handler can pre-empt deadlocks that handler. drain() does not take
/// it, and drain() from two contexts is unsupported and undetected (CR-4).
///
/// A line is dropped, not truncated, when the queue is full or the line exceeds
/// a slot; dropped() counts it (BR-5).
///
/// Every slot is sized for the longest line, so the queue costs
/// Slots x sizeof(Slot) of static RAM whatever the messages say. sizeof(Slot)
/// is LOG_LINE_SIZE plus the stored length, rounded up to the slot's alignment.
/// Choose Slots from the RAM available and the burst to absorb (AR-4).
template <std::size_t Slots, RingSinkLock Lock>
class RingSink final : public Sink {
    static_assert(Slots > 0, "a RingSink needs at least one slot");

   public:
    /// `target` receives the lines on drain() and is called only from the
    /// consumer context, so it need not be safe from interrupt context.
    /// `target` and `lock` must outlive this sink.
    RingSink(Sink& target, Lock& lock) noexcept
        : target_(&target), lock_(&lock) {}

    /// Producer context: copies the line into a free slot, or drops it.
    void write(std::string_view text) override {
        const Held held(*lock_);
        if (text.size() > LOG_LINE_SIZE) {
            // Longer than a slot: dropped and not truncated (BR-5).
            count_drop();
            return;
        }
        // acquire() and not push(): copying into the slot keeps a LOG_LINE_SIZE
        // temporary off the producer's stack, which a handler can least spare.
        Slot* slot = slots_.acquire();
        if (slot == nullptr) {
            count_drop();
            return;
        }
        std::copy_n(text.begin(), text.size(), slot->data.begin());
        slot->length = text.size();
        // commit() is the release that publishes the bytes above; without it
        // the consumer can see the count before the slot it counts.
        slots_.commit();
    }

    /// No-op: drain() flushes the target, out of the producer context.
    void flush() override {}

    /// Consumer context: forwards each queued line to the target (one write
    /// and one flush) and returns the number forwarded.
    std::size_t drain() {
        std::size_t forwarded = 0;
        while (const Slot* slot = slots_.front()) {
            target_->write(std::string_view(slot->data.data(), slot->length));
            target_->flush();
            // pop() releases the slot, after the write so a producer cannot
            // overwrite bytes the target has not read.
            slots_.pop();
            ++forwarded;
        }
        return forwarded;
    }

    /// Lines dropped for a full queue or an oversized line. Relaxed ordering,
    /// as it orders nothing (OR-6); safe from any context.
    [[nodiscard]] std::uint32_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

    /// Lines currently queued.
    [[nodiscard]] std::size_t size() const noexcept { return slots_.size(); }

    /// Slots in the ring, not bytes: each costs sizeof(Slot). A full ring drops
    /// the next line and never overwrites the oldest.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Slots;
    }

   private:
    struct Slot {
        std::array<char, LOG_LINE_SIZE> data{};
        std::size_t length = 0;
    };

    // Holds the producer lock for one write(). Not std::scoped_lock: <mutex> is
    // unusable in some freestanding configurations this header builds in.
    class Held {
       public:
        explicit Held(Lock& lock) : lock_(&lock) { lock_->lock(); }

        ~Held() { lock_->unlock(); }

        Held(const Held&) = delete;
        Held& operator=(const Held&) = delete;
        Held(Held&&) = delete;
        Held& operator=(Held&&) = delete;

       private:
        Lock* lock_;
    };

    // Called with the lock held, so this context is the counter's only writer
    // and a load and a store suffice; a read-modify-write has no race to close.
    void count_drop() noexcept {
        dropped_.store(dropped_.load(std::memory_order_relaxed) + 1U,
                       std::memory_order_relaxed);
    }

    // SpscCounter and not SingleContextCounter: the count must publish a
    // slot's bytes in both directions (CR-4). A single-context ring would pass
    // every host test and fail only on the target under pre-emption.
    pico_util::RingBuffer<Slot, Slots, pico_util::SpscCounter> slots_;
    Sink* target_;
    Lock* lock_;

    std::atomic<std::uint32_t> dropped_ = 0;
};

}  // namespace pico_logging
