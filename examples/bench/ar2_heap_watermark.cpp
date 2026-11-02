// SPDX-License-Identifier: MIT
// AR-2's device measurement: does the log path through StderrSink allocate?
//
// AR-2 states that an image linking StderrSink links an allocator -- newlib
// stdio's __smakebuf_r reaches _malloc_r -- and that under the SDK's default
// stdio the log path does not reach it. The second half depends on the stream's
// buffering mode, which the specification forbids settling by reading newlib's
// sources. This is the measurement that settles it instead.
//
// The shape is four samples, taken before anything is printed:
//
//   1. baseline, after stdio is up and before any log statement
//   2. after exactly one line through the default sink -- the event under test
//   3. after a sustained burst, which is where per-line growth would show
//   4. after a deliberate malloc, which the samples MUST see
//
// The fourth is the instrument's own negative control. Without it "no growth"
// and "the counter is dead" produce the same output, which is the failure mode
// this repository's gates exist to rule out. The control's block is published
// and written, because the compiler is free to delete an allocation whose
// result is unused.
//
// The primary instrument is therefore a count, not a heap delta. `_malloc_r`
// is the function __smakebuf_r calls, so wrapping it observes the exact event
// AR-2 is about -- and it observes malloc() too, since that is how malloc
// reaches the allocator. The SDK wraps `malloc`, a different symbol, so the
// two do not collide. mallinfo and the program break are kept alongside as
// secondary evidence rather than as the measurement.
//
// Nothing is printed until all four are captured: the reporting path uses
// stdout, and a buffer allocated for it would land in the middle of the
// measurement.

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
// newlib declares the malloc family with _NOTHROW in <stdlib.h> and without it
// in <malloc.h>. Clang models that attribute as an exception specification and
// rejects the declaration that adds one, so <stdlib.h> must be seen first.
//
// The preprocessor guard below is what keeps it that way: clang-format's
// include regrouping sorts the C header above the C++ one and a blank line
// does not survive it, but it will not sort across a directive.
#include <cstdlib>

#if defined(__has_include)
#if __has_include(<malloc.h>)
#include <malloc.h>
// A feature-detection flag, not a value: it must be testable by #if, which a
// constexpr constant cannot be.
#define PICO_LOGGING_HAVE_MALLINFO 1
#endif
#endif

#include "pico/stdlib.h"
#include "pico_logging/pico_logging.hpp"

namespace {
// The wrapper's counters. Written from the allocator path, read from main;
// this image is single-core and has no interrupt that allocates, so plain
// integers are what they are. `volatile` keeps the reads honest against a
// compiler that can see no writer.
volatile unsigned long malloc_calls = 0;
volatile unsigned long malloc_bytes = 0;
}  // namespace

// --wrap=_malloc_r, set in CMakeLists.txt. Every reference to _malloc_r from
// another translation unit -- newlib's __smakebuf_r among them -- resolves
// here, and __real__malloc_r is the allocator itself.
extern "C" {
void* __real__malloc_r(struct _reent* reent, std::size_t size);

void* __wrap__malloc_r(struct _reent* reent, std::size_t size) {
    malloc_calls = malloc_calls + 1;
    malloc_bytes = malloc_bytes + size;
    return __real__malloc_r(reent, size);
}
}

namespace {

/// One observation, taken at a point in the sequence above.
///
/// The call count is the measurement. The other two are corroboration and are
/// each blind in their own way: uordblks is newlib's count of in-use bytes,
/// which a free/alloc pair nets back to zero; the program break only ever
/// rises, so it cannot be fooled that way, but it does not move for an
/// allocation served from a free block.
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
    // Secondary evidence only. Its absence costs a column, not the
    // measurement: the count above is what settles AR-2.
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

// Long enough that per-line growth of even a few bytes is unmistakable, short
// enough to finish promptly over USB CDC.
constexpr int BURST_LINES = 500;

// Comfortably larger than any small-bin rounding, so the control's step is
// unambiguous.
constexpr std::size_t CONTROL_BYTES = 1024;
constexpr int CONTROL_FILL = 0xA5;

// The control's block, published where the optimiser cannot prove it dead and
// left allocated on purpose: freeing it would let a later reading of the heap
// see the space returned and read as if nothing had been taken.
void* volatile control_block = nullptr;

// The console is attached before the board is reset, so this normally returns
// at once. The bound is there so an unattended board still reaches the report
// rather than waiting forever.
constexpr int CONNECT_TIMEOUT_MS = 30000;
constexpr int CONNECT_POLL_MS = 100;

constexpr int REPORT_INTERVAL_MS = 1000;

// A value with a fractional part, so the burst exercises the float formatter
// rather than only the integer path.
constexpr double RECURRING = 1.0 / 3.0;

}  // namespace

int main() {
    stdio_init_all();

    for (int waited = 0; waited < CONNECT_TIMEOUT_MS && !stdio_usb_connected();
         waited += CONNECT_POLL_MS) {
        sleep_ms(CONNECT_POLL_MS);
    }

    // Every sample below is taken before the first printf. The default sink is
    // deliberately left in place: StderrSink is the subject, so nothing calls
    // set_sink here.
    pico_logging::set_level(pico_logging::Level::INFO);

    const Sample baseline = take_sample();

    PICO_LOG_INFO << "AR-2: the first line through the default sink";
    const Sample after_first_line = take_sample();

    for (int line = 0; line < BURST_LINES; ++line) {
        PICO_LOG_INFO << "AR-2 burst " << line << ' ' << RECURRING;
    }
    const Sample after_burst = take_sample();

    // The instrument's negative control. If this row shows nothing, every
    // other row on this page is meaningless.
    //
    // The result has to be published and the block written to. GCC's
    // allocation-DCE is entitled to delete a malloc/free pair whose result is
    // never used, and a deleted control reports no allocation because none
    // happened.
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
