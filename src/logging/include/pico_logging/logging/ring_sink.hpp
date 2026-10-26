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
/// A sink an interrupt handler can safely write to.
///
/// StderrSink is not safe from interrupt context: it calls std::fwrite, and
/// where newlib is built with FILE locking a handler that pre-empts a mainline
/// caller holding that FILE's lock deadlocks with no way out (CXX-08.3). Put a
/// RingSink in front of it: the producer copies its line into a slot and
/// returns, and the main loop calls drain() to forward whatever accumulated.
///
/// What this removes from the handler is std::fwrite, and no more. The line's
/// header is assembled before any sink is called, so the clock read,
/// localtime_r and strftime still run in the producer's context, and logging
/// from an interrupt handler is safe only where those calls are safe there
/// (CR-5) -- localtime_r and strftime take newlib's TZ lock, which is empty
/// unless a port retargets newlib's locking.

namespace pico_logging {

/// The longest line a LogLine can emit, and therefore one RingSink slot:
/// the header reservation, the message buffer, and the terminating newline.
/// Derived from the logging buffers rather than restated, so a slot cannot
/// silently stop being able to hold a full line.
inline constexpr std::size_t LOG_LINE_SIZE =
    LOG_HEADER_SIZE + LOG_BUFFER_CAPACITY + 1;

/// What RingSink needs of its producer lock: the standard's BasicLockable.
/// std::mutex satisfies it on a host; on the RP2350 a wrapper over the SDK's
/// critical_section does (see RingSink).
template <typename L>
concept RingSinkLock = requires(L& lock) {
    lock.lock();
    lock.unlock();
};

/// Fixed-capacity queue of whole log lines, written by any number of producer
/// contexts and drained by one consumer (the main loop). Allocates nothing and
/// blocks nowhere on the consumer side.
///
/// **Producers are serialised by `Lock`, which the caller supplies.** Installed
/// with set_sink(), a RingSink receives every log statement in the program --
/// the main loop's as well as any interrupt handler's or the second core's --
/// so it always has more than one producer. `Lock` must exclude every one of
/// them from every other: a lock that an interrupt handler can pre-empt the
/// holder of deadlocks the handler. On the RP2350 that is the SDK's
/// critical_section, which masks interrupts on the calling core and takes a
/// spinlock against the other:
///
///     struct CriticalSection {
///         critical_section_t section;
///         CriticalSection() { critical_section_init(&section); }
///         void lock() { critical_section_enter_blocking(&section); }
///         void unlock() { critical_section_exit(&section); }
///     };
///
/// The lock is held across one line's copy into its slot, at most
/// LOG_LINE_SIZE bytes, and for nothing else. drain() does not take it: there
/// is one consumer, and the slot counts order it against the producers.
///
/// **One consumer.** drain() from two contexts is not supported and is not
/// detected.
///
/// A line arrives whole or not at all: when the queue is full, or the line is
/// longer than a slot, the line is dropped and dropped() counts it. Partial
/// lines are never forwarded, which is what makes this composable with the
/// one-write-per-line guarantee the Sink contract carries.
///
/// **A slot is sized for the longest possible line, not for yours.** Every
/// slot is LOG_LINE_SIZE bytes plus its length, so the queue costs
/// Slots x sizeof(Slot) of static RAM whatever the messages actually say.
/// Choose Slots from the RAM available and the burst that must be absorbed,
/// not from how long the lines are (AR-4).
template <std::size_t Slots, RingSinkLock Lock>
class RingSink final : public Sink {
    static_assert(Slots > 0, "a RingSink needs at least one slot");

   public:
    /// `target` receives the forwarded lines on drain() and must outlive this
    /// sink. It is called only from the consumer context, so it does not
    /// itself need to be safe from interrupt context. `lock` serialises the
    /// producers and must outlive this sink too.
    RingSink(Sink& target, Lock& lock) noexcept
        : target_(&target), lock_(&lock) {}

    /// Producer context, from any number of them. Copies the line into a free
    /// slot, or drops it.
    void write(std::string_view text) override {
        const Held held(*lock_);
        if (text.size() > LOG_LINE_SIZE) {
            // Longer than a slot. Truncating would forward a partial line, so
            // the all-or-nothing policy applies here too (CXX-07.6).
            count_drop();
            return;
        }
        // acquire() rather than push(): the line is copied straight into the
        // ring's slot, so a full LOG_LINE_SIZE temporary never exists on the
        // producer's stack -- which for an interrupt handler is the stack that
        // can least afford it.
        Slot* slot = slots_.acquire();
        if (slot == nullptr) {
            count_drop();
            return;
        }
        std::copy_n(text.begin(), text.size(), slot->data.begin());
        slot->length = text.size();
        // commit() is the release that publishes the bytes written above.
        // Without it the consumer can see an incremented count before the
        // slot it counts.
        slots_.commit();
    }

    /// No-op: a line is already whole when it reaches write(), and forwarding
    /// is the consumer's job. Flushing from the producer context is exactly
    /// what this sink exists to avoid.
    void flush() override {}

    /// Consumer context. Forwards every queued line to the target, one write()
    /// per line followed by one flush(), and returns the number of lines
    /// forwarded.
    std::size_t drain() {
        std::size_t forwarded = 0;
        while (const Slot* slot = slots_.front()) {
            target_->write(std::string_view(slot->data.data(), slot->length));
            target_->flush();
            // pop() is the release marking the slot free, and it comes after
            // the write above so the producer cannot overwrite bytes the
            // target has not read yet.
            slots_.pop();
            ++forwarded;
        }
        return forwarded;
    }

    /// Lines discarded because the queue was full or the line oversized.
    /// Relaxed throughout: it is a statistic, ordered against nothing, and a
    /// reader that sees a slightly stale count has lost nothing (CXX-08.2).
    /// Safe from any context, lock or no lock.
    [[nodiscard]] std::uint32_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

    /// Lines currently queued.
    [[nodiscard]] std::size_t size() const noexcept { return slots_.size(); }

    /// Slots the ring holds, fixed at instantiation. size() reaching this means
    /// the next line is dropped and counted, never written over the oldest.
    ///
    /// A count of slots, not of bytes: each one costs sizeof(Slot), which is
    /// LOG_LINE_SIZE rounded up for the length beside it. See the class
    /// documentation before raising it.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Slots;
    }

   private:
    struct Slot {
        std::array<char, LOG_LINE_SIZE> data{};
        std::size_t length = 0;
    };

    // Holds the producer lock for one write(). Not std::scoped_lock: <mutex>
    // is not usable in every freestanding configuration this header builds in.
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

    // Called with the lock held, so this context is the counter's only writer,
    // and a load and a store suffice: with one writer there is no race for a
    // read-modify-write to close.
    void count_drop() noexcept {
        dropped_.store(dropped_.load(std::memory_order_relaxed) + 1U,
                       std::memory_order_relaxed);
    }

    // SpscCounter, named rather than defaulted: the lock makes the producers
    // one writer, the consumer is the main loop, and the count is what
    // publishes a slot's bytes in both directions. A single-context ring here
    // would pass every host test and fail only on the target under preemption,
    // which is the defect naming the policy prevents.
    pico_util::RingBuffer<Slot, Slots, pico_util::SpscCounter> slots_;
    Sink* target_;
    Lock* lock_;

    std::atomic<std::uint32_t> dropped_ = 0;
};

}  // namespace pico_logging
