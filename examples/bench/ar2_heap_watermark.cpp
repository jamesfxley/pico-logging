// SPDX-License-Identifier: MIT
// Device measurement of whether the log path through StderrSink allocates
// (AR-2). The method and conditions are in measurements.md beside it.
//
// The four samples, the last a negative control, are listed in measurements.md.
// Nothing is printed until all four are captured: printing uses stdout, and a
// buffer allocated for it would land mid-measurement.

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
// newlib declares the malloc family _NOTHROW in <stdlib.h> and not in
// <malloc.h>. Clang models that as an exception specification and rejects the
// redeclaration, so <stdlib.h> must be seen first.
//
// The preprocessor guard below is what keeps it that way: clang-format's
// include regrouping sorts the C header above the C++ one and a blank line
// does not survive it, but it does not sort across a directive.
#include <cstdlib>

#if defined(__has_include)
#if __has_include(<malloc.h>)
#include <malloc.h>
// A flag tested by #if, which a constexpr constant cannot be.
#define PICO_LOGGING_HAVE_MALLINFO 1
#endif
#endif

#include "pico/stdlib.h"
#include "pico_logging/pico_logging.hpp"

namespace {
// The wrapper's counters, written from the allocator path and read from main.
// The image is single-core with no allocating interrupt, so plain integers
// suffice; `volatile` stops the compiler assuming there is no writer.
volatile unsigned long malloc_calls = 0;
volatile unsigned long malloc_bytes = 0;
}  // namespace

// --wrap=_malloc_r (CMakeLists.txt): every reference to _malloc_r from another
// translation unit, newlib's __smakebuf_r among them, resolves here.
// __real__malloc_r is the allocator.
extern "C" {
void* __real__malloc_r(struct _reent* reent, std::size_t size);

void* __wrap__malloc_r(struct _reent* reent, std::size_t size) {
    malloc_calls = malloc_calls + 1;
    malloc_bytes = malloc_bytes + size;
    return __real__malloc_r(reent, size);
}
}

namespace {

/// One observation. The call count is the measurement; the other two
/// corroborate it and are each blind in their own way: uordblks nets a
/// free/alloc pair back to zero, and the program break only rises but does not
/// move for an allocation served from a free block.
struct Sample {
    unsigned long calls;
    unsigned long bytes;
    std::size_t in_use_bytes;
    std::uintptr_t program_break;
};

Sample take_sample() {
#if defined(PICO_LOGGING_HAVE_MALLINFO)
    const struct mallinfo info = mallinfo();
    const auto in_use = static_cast<std::size_t>(info.uordblks);
#else
    // Secondary evidence: its absence costs a column, not the result (AR-2).
    const std::size_t in_use = 0;
#endif
    return Sample{malloc_calls, malloc_bytes, in_use,
                  reinterpret_cast<std::uintptr_t>(sbrk(0))};
}

void report_row(const char* label, const Sample& sample, const Sample& base) {
    std::printf("%-32s %8lu %9lu  %8zu  0x%08lx\n", label,
                sample.calls - base.calls, sample.bytes - base.bytes,
                sample.in_use_bytes,
                static_cast<unsigned long>(sample.program_break));
}

// Long enough that per-line growth of a few bytes shows, short enough to finish
// over USB CDC.
constexpr int BURST_LINES = 500;

// Larger than any small-bin rounding.
constexpr std::size_t CONTROL_BYTES = 1024;
constexpr int CONTROL_FILL = 0xA5;

// Published where the optimiser cannot prove it dead, and left allocated:
// freeing it would let a later heap reading see the space returned.
void* volatile control_block = nullptr;

// The console is attached before the reset, so the wait below normally ends at
// once; the bound lets an unattended board carry on.
constexpr int CONNECT_TIMEOUT_MS = 30000;
constexpr int CONNECT_POLL_MS = 100;

constexpr int REPORT_INTERVAL_MS = 1000;

// A fractional value, so the burst exercises the float formatter.
constexpr double RECURRING = 1.0 / 3.0;

}  // namespace

int main() {
    stdio_init_all();

    for (int waited = 0; waited < CONNECT_TIMEOUT_MS && !stdio_usb_connected();
         waited += CONNECT_POLL_MS) {
        sleep_ms(CONNECT_POLL_MS);
    }

    // Every sample is taken before the first printf. The default sink stays:
    // StderrSink is the subject.
    pico_logging::set_level(pico_logging::Level::INFO);

    const Sample baseline = take_sample();

    PICO_LOG_INFO << "AR-2: the first line through the default sink";
    const Sample after_first_line = take_sample();

    for (int line = 0; line < BURST_LINES; ++line) {
        PICO_LOG_INFO << "AR-2 burst " << line << ' ' << RECURRING;
    }
    const Sample after_burst = take_sample();

    // The negative control: if this row shows nothing, every other row is
    // meaningless. The result is published and written to because GCC can
    // delete an allocation whose result is unused.
    void* control = std::malloc(CONTROL_BYTES);
    control_block = control;
    if (control != nullptr) {
        std::memset(control, CONTROL_FILL, CONTROL_BYTES);
    }
    const Sample after_control = take_sample();

    while (true) {
        std::printf("\n== AR-2 heap watermark: pico-logging %d.%d.%d ==\n",
                    pico_logging::VERSION_MAJOR, pico_logging::VERSION_MINOR,
                    pico_logging::VERSION_PATCH);
        std::printf("board=%s sdk=%s burst=%d control=%zu@%p BUFSIZ=%d\n",
                    PICO_LOGGING_DEVICE_BOARD, PICO_SDK_VERSION_STRING,
                    BURST_LINES, CONTROL_BYTES,
                    static_cast<void*>(control_block), BUFSIZ);
        std::printf("%-32s %8s %9s  %8s  %10s\n", "sample since baseline",
                    "_malloc_r", "bytes", "uordblks", "brk");
        report_row("1 baseline, before any log", baseline, baseline);
        report_row("2 after one StderrSink line", after_first_line, baseline);
        report_row("3 after burst", after_burst, baseline);
        report_row("4 after control malloc", after_control, baseline);
        report_row("live", take_sample(), baseline);

        if (after_control.calls <= after_burst.calls) {
            std::printf(
                "INSTRUMENT DEAD: the control allocation was not observed; "
                "no row above means anything\n");
        } else if (after_burst.calls == baseline.calls) {
            std::printf(
                "RESULT: the log path through StderrSink reached _malloc_r 0 "
                "times across %d lines\n",
                BURST_LINES + 1);
        } else {
            std::printf(
                "RESULT: the log path through StderrSink reached _malloc_r %lu "
                "times for %lu bytes across %d lines; the first line accounts "
                "for %lu of them\n",
                after_burst.calls - baseline.calls,
                after_burst.bytes - baseline.bytes, BURST_LINES + 1,
                after_first_line.calls - baseline.calls);
        }

        sleep_ms(REPORT_INTERVAL_MS);
    }
}
