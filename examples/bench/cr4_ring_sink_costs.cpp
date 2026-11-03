// SPDX-License-Identifier: MIT
// AR-5, CR-4. What a log statement costs the context it runs in, through a
// RingSink behind the SDK's critical_section and through the default
// StderrSink.
//
// Two figures, each with its own negative control:
//
//   Stack depth. The stack below the caller is painted with a pattern, one
//   statement runs, and the deepest word no longer holding the pattern is how
//   far the statement reached -- its own frames, the formatter's, and the
//   libc calls the timestamp makes. The control is a function whose frame is
//   known to be at least CONTROL_FRAME bytes: a depth below that means the
//   paint or the scan is broken and no other row means anything.
//
//   Lock hold time. The producer lock is the SDK's critical_section, which
//   masks interrupts on this core for as long as it is held. A wrapper reads
//   SysTick at lock() and at unlock(), so the figure is the interval
//   interrupts were masked, from inside it. The control holds the same lock
//   across a busy-wait of known length and must read as at least that.
//
// Interrupts are masked around each stack measurement so that nothing else
// writes the painted region while it is being read. Nothing is printed until
// every figure is captured.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/structs/systick.h"
#include "hardware/sync.h"
#include "pico/critical_section.h"
#include "pico/stdlib.h"
#include "pico_logging/pico_logging.hpp"

// The end of the linker's .scratch_y contents. The main stack occupies the rest
// of SCRATCH_Y above it, so the painted region never overlaps placed data.
extern "C" char __scratch_y_end__;

namespace {

// RingSink's producer lock on this platform, as ring_sink.hpp documents it,
// with SysTick read inside the masked interval so the hold time is measured
// from where interrupts are actually off.
class TimedCriticalSection {
   public:
    TimedCriticalSection() { critical_section_init(&section_); }

    void lock() {
        critical_section_enter_blocking(&section_);
        entered_ = systick_hw->cvr;
    }

    void unlock() {
        // SysTick counts down, and the reload is its full 24 bits, so the
        // difference modulo 2^24 is the elapsed count for any hold shorter
        // than one wrap -- about 110 ms at 150 MHz.
        const std::uint32_t left = systick_hw->cvr;
        const std::uint32_t held = (entered_ - left) & SYSTICK_MASK;
        if (held > longest_) {
            longest_ = held;
        }
        critical_section_exit(&section_);
    }

    [[nodiscard]] std::uint32_t longest_cycles() const { return longest_; }

    void reset() { longest_ = 0; }

    static constexpr std::uint32_t SYSTICK_MASK = 0x00FFFFFFU;

   private:
    critical_section_t section_{};
    std::uint32_t entered_ = 0;
    std::uint32_t longest_ = 0;
};

// Counts what it is handed and keeps nothing, so the drain side costs little
// and never touches stdio during a measurement.
class CountingSink final : public pico_logging::Sink {
   public:
    void write(std::string_view text) override { bytes_ += text.size(); }

    [[nodiscard]] std::size_t bytes() const { return bytes_; }

   private:
    std::size_t bytes_ = 0;
};

CountingSink target;
TimedCriticalSection lock;
pico_logging::RingSink<4, TimedCriticalSection> ring(target, lock);

constexpr std::uint32_t PAINT = 0xDEADBEEFU;
// Words left unpainted directly below the caller's frame, so painting does not
// overwrite the frame of the function doing the painting.
constexpr std::uintptr_t PAINT_GUARD = 128;

std::uintptr_t stack_pointer() {
    std::uintptr_t sp = 0;
    asm volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

std::uintptr_t paint_floor() {
    return reinterpret_cast<std::uintptr_t>(&__scratch_y_end__);
}

// Paints from the floor up to just below `top`.
[[gnu::noinline]] void paint(std::uintptr_t top) {
    for (std::uintptr_t word = paint_floor(); word < top - PAINT_GUARD;
         word += sizeof(std::uint32_t)) {
        *reinterpret_cast<volatile std::uint32_t*>(word) = PAINT;
    }
}

// The lowest address below `top` no longer holding the pattern, as a depth
// below `top`. Zero means nothing below the guard was touched.
[[gnu::noinline]] std::uintptr_t depth_below(std::uintptr_t top) {
    for (std::uintptr_t word = paint_floor(); word < top - PAINT_GUARD;
         word += sizeof(std::uint32_t)) {
        if (*reinterpret_cast<volatile std::uint32_t*>(word) != PAINT) {
            return top - word;
        }
    }
    return 0;
}

// The deepest stack any of `statement`'s calls reached, measured from this
// function's own stack pointer, with interrupts masked for the duration.
template <typename Statement>
[[gnu::noinline]] std::uintptr_t measure_depth(Statement statement) {
    const std::uint32_t saved = save_and_disable_interrupts();
    const std::uintptr_t top = stack_pointer();
    paint(top);
    statement();
    const std::uintptr_t depth = depth_below(top);
    restore_interrupts(saved);
    return depth;
}

// As measure_depth, but with the USB interrupt left live and every other one
// disabled at the NVIC rather than masked: the StderrSink path calls the USB
// stack's tud_task() inline, and only with the USB interrupt running are there
// transfer events for it to dispatch below the statement's own frames. The
// USB handler's own frames may land in the figure too, which overstates it,
// so the result is still a bound. PRIMASK is untouched.
template <typename Statement>
[[gnu::noinline]] std::uintptr_t measure_depth_usb_live(Statement statement) {
    std::array<bool, NUM_IRQS> enabled{};
    for (unsigned int irq = 0; irq < NUM_IRQS; ++irq) {
        enabled[irq] = irq_is_enabled(irq);
        if (irq != USBCTRL_IRQ) {
            irq_set_enabled(irq, false);
        }
    }
    const std::uintptr_t top = stack_pointer();
    paint(top);
    statement();
    const std::uintptr_t depth = depth_below(top);
    for (unsigned int irq = 0; irq < NUM_IRQS; ++irq) {
        if (irq != USBCTRL_IRQ) {
            irq_set_enabled(irq, enabled[irq]);
        }
    }
    return depth;
}

// The stack control's frame. Touched through a volatile pointer so the array
// cannot be elided, and noinline so it is a frame of its own.
constexpr std::size_t CONTROL_FRAME = 512;

[[gnu::noinline]] void control_frame() {
    std::array<volatile std::uint8_t, CONTROL_FRAME> frame{};
    for (auto& byte : frame) {
        byte = 1;
    }
}

// The lock control's hold, in microseconds.
constexpr std::uint32_t CONTROL_HOLD_US = 10;

// A line of the longest length LogLine can emit, which is the most any one
// hold of the lock can copy.
std::array<char, pico_logging::LOG_LINE_SIZE> longest_line{};

constexpr int LOCK_WRITES = 1000;
constexpr int DRAIN_EVERY = 3;

constexpr double FIXED = 1.0 / 3.0;
constexpr double TINY = 1.0e-7;
constexpr std::int32_t WIDE = -2000000000;

constexpr int CONNECT_TIMEOUT_MS = 30000;
constexpr int CONNECT_POLL_MS = 100;
constexpr int REPORT_INTERVAL_MS = 1000;

struct Row {
    const char* label;
    std::uintptr_t bytes;
};

}  // namespace

int main() {
    stdio_init_all();
    for (int waited = 0; waited < CONNECT_TIMEOUT_MS && !stdio_usb_connected();
         waited += CONNECT_POLL_MS) {
        sleep_ms(CONNECT_POLL_MS);
    }

    systick_hw->rvr = TimedCriticalSection::SYSTICK_MASK;
    systick_hw->cvr = 0;
    // Enable, processor clock, no interrupt.
    systick_hw->csr = 0x5U;

    pico_logging::set_sink(&ring);
    pico_logging::set_level(pico_logging::Level::TRACE);

    // The first statement is measured first on purpose: it is the one that
    // initialises the libc's time-zone state, which later statements skip.
    const std::array<Row, 6> depths{{
        {"control frame (>= 512)", measure_depth([] { control_frame(); })},
        {"first statement, int",
         measure_depth([] { PICO_LOG_WARN << "first " << WIDE; })},
        {"int and fixed double",
         measure_depth([] { PICO_LOG_WARN << "v " << WIDE << ' ' << FIXED; })},
        {"exponent double",
         measure_depth([] { PICO_LOG_WARN << "e " << TINY; })},
        {"string_view and bool", measure_depth([] {
             PICO_LOG_WARN << std::string_view("view ") << true << ' ' << 'c';
         })},
        {"message cut at capacity", measure_depth([] {
             PICO_LOG_WARN << std::string_view(
                                  longest_line.data(),
                                  pico_logging::LOG_BUFFER_CAPACITY)
                           << "tail";
         })},
    }};
    ring.drain();

    // AR-5 again through the default StderrSink, the path most consumers run:
    // fwrite into newlib's stdio, then the SDK's USB CDC driver, with the USB
    // interrupt live so the driver has transfer events to dispatch. Without a
    // host attached the driver is never called, so that is recorded and the
    // rows are refused rather than read as covering it.
    const bool usb_reached = stdio_usb_connected();
    pico_logging::set_sink(nullptr);
    const std::array<Row, 5> stderr_depths{{
        {"stderr: int",
         measure_depth_usb_live([] { PICO_LOG_WARN << "first " << WIDE; })},
        {"stderr: int and fixed double", measure_depth_usb_live([] {
             PICO_LOG_WARN << "v " << WIDE << ' ' << FIXED;
         })},
        {"stderr: exponent double",
         measure_depth_usb_live([] { PICO_LOG_WARN << "e " << TINY; })},
        {"stderr: string_view and bool", measure_depth_usb_live([] {
             PICO_LOG_WARN << std::string_view("view ") << true << ' ' << 'c';
         })},
        {"stderr: message cut", measure_depth_usb_live([] {
             PICO_LOG_WARN << std::string_view(
                                  longest_line.data(),
                                  pico_logging::LOG_BUFFER_CAPACITY)
                           << "tail";
         })},
    }};
    pico_logging::set_sink(&ring);

    for (char& byte : longest_line) {
        byte = 'x';
    }
    const std::string_view longest(longest_line.data(), longest_line.size());
    lock.reset();
    for (int write = 0; write < LOCK_WRITES; ++write) {
        ring.write(longest);
        if (write % DRAIN_EVERY == 0) {
            ring.drain();
        }
    }
    ring.drain();
    const std::uint32_t longest_hold = lock.longest_cycles();

    lock.reset();
    lock.lock();
    busy_wait_us_32(CONTROL_HOLD_US);
    lock.unlock();
    const std::uint32_t control_hold = lock.longest_cycles();

    const std::uint32_t hz = clock_get_hz(clk_sys);
    const std::uint32_t cycles_per_us = hz / 1000000U;
    std::uintptr_t deepest = 0;
    for (std::size_t row = 1; row < depths.size(); ++row) {
        if (depths[row].bytes > deepest) {
            deepest = depths[row].bytes;
        }
    }
    std::uintptr_t deepest_stderr = 0;
    for (const Row& row : stderr_depths) {
        if (row.bytes > deepest_stderr) {
            deepest_stderr = row.bytes;
        }
    }

    while (true) {
        std::printf("\n== CR-4 RingSink costs: pico-logging %d.%d.%d ==\n",
                    pico_logging::VERSION_MAJOR, pico_logging::VERSION_MINOR,
                    pico_logging::VERSION_PATCH);
        std::printf(
            "board=%s build=%s stdio=%s sdk=%s clk_sys=%lu Hz line=%zu "
            "forwarded=%zu dropped=%lu\n",
            PICO_LOGGING_DEVICE_BOARD, PICO_LOGGING_DEVICE_BUILD_TYPE,
            PICO_LOGGING_DEVICE_STDIO, PICO_SDK_VERSION_STRING,
            static_cast<unsigned long>(hz), pico_logging::LOG_LINE_SIZE,
            target.bytes(), static_cast<unsigned long>(ring.dropped()));
        std::printf("%-28s %8s\n", "stack depth", "bytes");
        for (const Row& row : depths) {
            std::printf("%-28s %8lu\n", row.label,
                        static_cast<unsigned long>(row.bytes));
        }
        for (const Row& row : stderr_depths) {
            std::printf("%-28s %8lu\n", row.label,
                        static_cast<unsigned long>(row.bytes));
        }
        std::printf("%-28s %8s %8s\n", "lock held", "cycles", "us");
        std::printf("%-28s %8lu %8lu\n", "control (>= 10 us)",
                    static_cast<unsigned long>(control_hold),
                    static_cast<unsigned long>(control_hold / cycles_per_us));
        std::printf("%-28s %8lu %8lu\n", "longest line, worst of 1000",
                    static_cast<unsigned long>(longest_hold),
                    static_cast<unsigned long>(longest_hold / cycles_per_us));

        if (depths[0].bytes < CONTROL_FRAME) {
            std::printf(
                "STACK INSTRUMENT DEAD: the control frame was not "
                "observed; no depth above means anything\n");
        } else {
            std::printf(
                "RESULT: deepest log statement reached %lu bytes "
                "below its caller through RingSink\n",
                static_cast<unsigned long>(deepest));
            if (usb_reached) {
                std::printf(
                    "RESULT: deepest log statement reached %lu bytes "
                    "below its caller through StderrSink, USB live\n",
                    static_cast<unsigned long>(deepest_stderr));
            } else {
                std::printf(
                    "STDERR PATH NOT REACHED: no USB host was connected, "
                    "so the stderr rows never called the driver\n");
            }
        }
        if (control_hold < CONTROL_HOLD_US * cycles_per_us) {
            std::printf(
                "LOCK INSTRUMENT DEAD: the control hold was not "
                "observed; no hold time above means anything\n");
        } else {
            std::printf(
                "RESULT: RingSink held the lock at most %lu cycles "
                "for a %zu-byte line\n",
                static_cast<unsigned long>(longest_hold),
                pico_logging::LOG_LINE_SIZE);
        }
        sleep_ms(REPORT_INTERVAL_MS);
    }
}
