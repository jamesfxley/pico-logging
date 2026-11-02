// SPDX-License-Identifier: MIT
// CR-4: mainline and hardware-alarm IRQ producers share the SDK lock.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "hardware/timer.h"
#include "pico/critical_section.h"
#include "pico/platform/common.h"
#include "pico/stdio.h"
#include "pico/time.h"
#include "pico/types.h"
#include "pico_family/device_test.hpp"
#include "pico_logging/logging/ring_sink.hpp"
#include "pico_logging/logging/sinks.hpp"

namespace {

constexpr std::size_t RECORDS_PER_PRODUCER = 32;
constexpr std::size_t PRODUCERS = 2;
constexpr std::size_t TOTAL_RECORDS = RECORDS_PER_PRODUCER * PRODUCERS;
constexpr std::size_t RING_SLOTS = 4;
using Record = std::array<char, pico_logging::LOG_LINE_SIZE>;
static_assert(pico_logging::LOG_LINE_SIZE >= 17 + RECORDS_PER_PRODUCER);
static_assert(RECORDS_PER_PRODUCER < 128);

struct Offered {
    Record bytes{};
    std::size_t length = 0;
};

// Each record carries its producer and sequence, including embedded zeroes.
// Alternating short and maximum-length records exercise whole-slot copying.
Offered offered(std::size_t producer, std::size_t sequence) {
    Offered result;
    result.length = sequence % 2 == 0 ? result.bytes.size() : 17 + sequence;
    for (std::size_t index = 0; index < result.length; ++index) {
        result.bytes[index] =
            static_cast<char>('a' + ((index + sequence + producer) % 26));
    }
    result.bytes[0] = static_cast<char>(producer);
    result.bytes[1] = static_cast<char>(sequence);
    return result;
}

class Capture final : public pico_logging::Sink {
   public:
    void write(std::string_view text) override {
        if (count_ >= records_.size() || text.size() > Record{}.size()) {
            overflow_ = true;
            return;
        }
        auto& record = records_[count_];
        std::ranges::copy(text, record.bytes.begin());
        record.length = text.size();
        ++count_;
    }

    void flush() override {}

    [[nodiscard]] bool valid() const {
        std::array<std::size_t, PRODUCERS> next{};
        std::array<std::size_t, PRODUCERS> counts{};
        for (std::size_t index = 0; index < count_; ++index) {
            const auto& record = records_[index];
            if (record.length < 2) {
                return false;
            }
            const auto producer = static_cast<unsigned char>(record.bytes[0]);
            const auto sequence = static_cast<unsigned char>(record.bytes[1]);
            if (producer >= PRODUCERS || sequence >= RECORDS_PER_PRODUCER ||
                sequence < next[producer]) {
                return false;
            }
            const auto expected = offered(producer, sequence);
            if (record.length != expected.length ||
                std::string_view(record.bytes.data(), record.length) !=
                    std::string_view(expected.bytes.data(), expected.length)) {
                return false;
            }
            next[producer] = sequence + 1U;
            ++counts[producer];
        }
        return !overflow_ && counts[0] != 0 && counts[1] != 0;
    }

    [[nodiscard]] std::size_t count() const { return count_; }

   private:
    std::array<Offered, TOTAL_RECORDS> records_{};
    std::size_t count_ = 0;
    bool overflow_ = false;
};

class CriticalSection final {
   public:
    CriticalSection() { critical_section_init(&section_); }

    ~CriticalSection() { critical_section_deinit(&section_); }

    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
    CriticalSection(CriticalSection&&) = delete;
    CriticalSection& operator=(CriticalSection&&) = delete;

    void arm_irq(std::uint32_t alarm) {
        critical_section_enter_blocking(&section_);
        alarm_ = alarm;
        request_irq_ = true;
        critical_section_exit(&section_);
    }

    void lock() {
        critical_section_enter_blocking(&section_);
        if (request_irq_) {
            request_irq_ = false;
            // SDK hardware/timer.h documents force_irq as calling the callback
            // from IRQ context after this call. Pended with interrupts masked,
            // it must wait for unlock before the IRQ producer can take this
            // same critical_section. No timer deadline or scheduling luck.
            hardware_alarm_force_irq(alarm_);
        }
    }

    void unlock() { critical_section_exit(&section_); }

   private:
    critical_section_t section_{};
    // Both fields are protected by section_, including the IRQ's lock().
    std::uint32_t alarm_ = 0;
    bool request_irq_ = false;
};

class Workload final {
   public:
    static Workload& instance() {
        // SDK alarm callbacks have no context pointer. This fixed storage
        // outlives the callback; mainline constructs it before enabling IRQs.
        static Workload workload;
        return workload;
    }

    static void interrupt(uint /*alarm*/) {
        auto& workload = instance();
        // Only the IRQ writes completed_; the acquire in run() publishes its
        // write's completion before the next mainline request.
        const auto sequence =
            workload.completed_.load(std::memory_order_relaxed);
        if (sequence < RECORDS_PER_PRODUCER) {
            const auto record = offered(1, sequence);
            workload.ring_.write({record.bytes.data(), record.length});
            workload.completed_.store(sequence + 1, std::memory_order_release);
        }
    }

    [[nodiscard]] bool run(std::uint32_t alarm) {
        const auto deadline = make_timeout_time_ms(5000);
        for (std::size_t sequence = 0; sequence < RECORDS_PER_PRODUCER;
             ++sequence) {
            const auto record = offered(0, sequence);
            lock_.arm_irq(alarm);
            ring_.write({record.bytes.data(), record.length});
            while (completed_.load(std::memory_order_acquire) != sequence + 1) {
                if (time_reached(deadline)) {
                    return false;
                }
                tight_loop_contents();
            }
            // Eight offers per batch into four slots force whole-record drops.
            // Repeating the drain forces slot reuse in both producer contexts.
            if ((sequence + 1) % RING_SLOTS == 0) {
                if (ring_.drain() != RING_SLOTS) {
                    return false;
                }
            }
        }
        return ring_.size() == 0 && ring_.dropped() == TOTAL_RECORDS / 2 &&
               capture_.count() == TOTAL_RECORDS / 2 &&
               capture_.count() + ring_.dropped() == TOTAL_RECORDS &&
               capture_.valid();
    }

   private:
    Capture capture_;
    CriticalSection lock_;
    pico_logging::RingSink<RING_SLOTS, CriticalSection> ring_{capture_, lock_};
    std::atomic<std::size_t> completed_{0};
};

}  // namespace

int main() {
    stdio_init_all();
    pico_family::device_test::Run run;
    auto& workload = Workload::instance();
    const int claimed = hardware_alarm_claim_unused(false);
    bool passed = false;
    if (claimed >= 0) {
        const auto alarm = static_cast<std::uint32_t>(claimed);
        hardware_alarm_set_callback(alarm, Workload::interrupt);
        passed = workload.run(alarm);
        hardware_alarm_set_callback(alarm, nullptr);
        hardware_alarm_unclaim(alarm);
    }
    run.check("CR-4", "ring-sink-irq-producers-preserve-lines", passed,
              passed
                  ? "whole ordered records; full-ring drops and reuse"
                  : "alarm, completion, record integrity or accounting failed");
    run.finish();
}
