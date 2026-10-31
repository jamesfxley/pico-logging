# Using pico-logging

The README covers the surface: the five macros, the two filters, the `Sink`
interface. This is the other half — bringing it up in a real image, and the
four things that bite.

## Bringing it up in firmware

The [README](../README.md)'s Quick start shows plain `FetchContent`. A family project declares product pins directly in CMake with
`pico_family_dependency()` followed by `pico_family_make_available()`.

```cmake
target_link_libraries(my_firmware PRIVATE pico_logging::core pico_stdlib)
```

Nothing needs initialising. `StderrSink` is installed by default and the SDK
routes stderr to whichever stdio you enabled, so a `PICO_LOG_INFO` before any
setup already reaches the console:

<!-- example: firmware/guide_bring_up -->
```cpp
#include <pico_logging/logging/logging.hpp>

#include "pico/stdlib.h"

int main() {
    stdio_init_all();
    PICO_LOG_INFO << "boot";
    // ... the application's own work
}
```

Install your own sink only when the default destination is wrong. It must
outlive every logging statement, so give it static storage:

<!-- example: guide_set_sink -->
```cpp
void install_uart_sink() {
    static UartSink uart_sink;            // must outlive all logging
    pico_logging::set_sink(&uart_sink);   // nullptr restores StderrSink
}
```

## Choosing between the two filters

They are not alternatives; they answer different questions.

**The compile-time ceiling is a build decision.** Set it project-wide with
`-DPICO_LOGGING_LEVEL=3`, which becomes a `PUBLIC` compile definition so the
library and every consumer agree. A statement above the ceiling is discarded by
`if constexpr`, so neither the statement nor its operands are evaluated and
neither reaches the image. This is the one that costs flash.

**The runtime level is an operational decision.** `set_level(Level::DBG)` from
a console command, then back. A statement below the ceiling but above the
runtime level is constructed and discarded, so its operands *are* evaluated —
which is why a `PICO_LOG_DEBUG << expensive()` costs something even when
nothing is printed. Guard those:

<!-- example: guide_debug_guard -->
```cpp
if (PICO_LOG_ENABLED_DBG) {
    PICO_LOG_DEBUG << expensive_summary();
}
```

Rule of thumb: set the ceiling to the most verbose level the image should ever
be *able* to emit, and use the runtime level to move within it.

## The ceiling is per translation unit, and that is deliberate

`LOGGING_GLOBAL_LEVEL` is a macro, and no entity this library links carries its
value, so compiling one file at a different level from the rest is supported
rather than merely tolerated — a noisy driver can be built at `TRACE` inside an
image built at `INFO`.

The consequence to know: the level a statement obeys is the level of the file
it is written in, not the file that called it. A helper compiled at `INFO` logs
at `INFO` however verbose its caller is. If a subsystem's logging seems to
ignore the level you set, check which translation unit the statement lives in.

The test tree pins this: `tests/logging/level_odr/` compiles the same source at
two levels and links both, and a structural check fails if the ceiling reaches
anything but a macro, so the property cannot regress into a one-definition-rule
violation.

**The constraint you inherit.** A `PICO_LOG_*` macro expands where you write
it, so a statement inside an `inline` function or a function template in one of
*your* headers puts the ceiling into a linked entity — yours, not this
library's. Two of your translation units at different ceilings then hold two
definitions of that function, which is the violation above with the roles
moved. It does not arise while every target shares one ceiling, which
`-DPICO_LOGGING_LEVEL` gives you; it becomes reachable the moment you override
the ceiling on one target. Log from a header at your own ceiling, or not from a
header.

## Keeping the no-allocation guarantee

The log path must not reach newlib's arbitrary-precision float formatter. That
is what `snprintf("%g")` pulls in: `_dtoa_r` calls `_Balloc`, which calls
`_malloc_r` whenever its bucket freelist is empty. This library formats floats
into fixed stack scratch and reaches none of it.

Two consequences for your code:

- **Do not format log output with `printf` or `snprintf` and pass the result
  in.** You reintroduce exactly what the gate forbids, and the gate will not
  see it — it measures this library's image, not your formatting call.
- **Float output is fixed-point with six decimals over a bounded range, and
  exponent notation outside it.** A magnitude from `FLOAT_FIXED_MIN` up to
  `FLOAT_FIXED_MAX` renders as `ddd.dddddd`, correctly rounded; anything
  smaller or larger renders as `d.dddddde±dd`, whose last places are
  approximate (FR-5). A parser reading your log lines has to accept both. If
  you need `%g` semantics, format it yourself outside the log path and log the
  string.

The `symbols` gate (`python3 dev.py gate symbols`) is the check, and a control asserts it rejects a
deliberately allocating image as well as accepting the real one.

## What a line carries

- **Message text is escaped.** A control byte renders as `\n`, `\r`, `\t` or
  `\xHH` and a backslash as `\\`, so one statement is always one line and no
  text can forge a record (FR-1). UTF-8 passes through. A parser that wants the
  text back has to unescape it. A line cut at capacity keeps an escape whole or
  drops it.
- **A file built with fast or unsafe floating-point math cannot log a float.**
  Under `-ffast-math`, `-ffinite-math-only`, `-funsafe-math-optimizations` or
  `-fno-signed-zeros` the formatter's rendering cannot be promised, so a
  statement streaming a `float` or `double` fails to compile there with a
  message naming FR-5; text and integers log as usual. `-fassociative-math`
  takes effect only alongside `-fno-signed-zeros`, so it is refused with it.
  Clang detects only the first two flags, so a clang build with the others is
  not refused.

## Logging from an interrupt handler

`StderrSink` is not safe from interrupt context. It calls `std::fwrite`, and
where newlib is built with FILE locking a handler pre-empting a mainline caller
that holds the lock deadlocks (`CR-3`). Whether it also allocates depends on
the stream's buffering: a fully buffered `stderr` allocates its `FILE` buffer
on first write, and the SDK's default configuration does not — `AR-2`'s device
run counts no allocation across a burst through the default sink. Put a
`RingSink` in front of it, with the SDK's `critical_section` as its lock:

<!-- example: firmware/guide_ring_sink -->
```cpp
#include "pico/critical_section.h"
#include "pico_logging/logging/ring_sink.hpp"

// RingSink's producer lock: masks interrupts on this core and takes a hardware
// spinlock against the other, so every context that logs is excluded from
// every other.
struct CriticalSection {
    critical_section_t section{};
    CriticalSection() { critical_section_init(&section); }
    void lock() { critical_section_enter_blocking(&section); }
    void unlock() { critical_section_exit(&section); }
};

pico_logging::RingSink<8, CriticalSection>& log_ring() {
    static pico_logging::StderrSink console;
    static CriticalSection ring_lock;
    // 8 slots. A slot holds a whole line, so this costs 8 x sizeof(slot), a
    // little over 4 KiB of static RAM on a 32-bit target -- size it against
    // your RAM budget, not against how long your messages are.
    static pico_logging::RingSink<8, CriticalSection> ring(console, ring_lock);
    return ring;
}

void start_logging() { pico_logging::set_sink(&log_ring()); }   // once, at boot
void service_logging() { log_ring().drain(); }                  // in the main loop
```

Every log statement now goes through the ring — the main loop's, every
handler's, and the second core's. Each line is copied into a slot under the
lock and the main loop forwards it. Whole lines only: when the queue is full,
or a line is longer than a slot, the line is dropped and `dropped()` counts it.
Report that counter — a silent drop and a quiet system look identical.

**The lock is why several contexts can log.** A `RingSink` is written by every
context that logs, so its lock has to exclude each of them from every other.
One that an interrupt handler can pre-empt the holder of — a mutex that does
not mask interrupts — deadlocks the handler. The lock is held across one line's
copy into its slot and nothing else, so interrupts stay masked for at most the
time it takes to copy `LOG_LINE_SIZE` bytes; `tests/device/measurements.md`
describes the run that measures it on an RP2350.

**One consumer.** Call `drain()` from one context only, normally the main loop.
It does not take the lock.

**Size the stack for two statements.** A log statement reaches up to 1.75 KiB of
stack below its caller (`AR-5`; its measurement on 0.1.0 is FW-3), and a handler
that logs while the main loop is inside a statement puts both on the main stack:
3.5 KiB, plus the exception frame (up to 104 bytes with floating-point context).
That exceeds the Pico SDK's default `PICO_STACK_SIZE` of 2 KiB, and an overflow
there is silent. Raise it to 4 KiB in an image whose handlers log, which is the
most it can be: the SDK places core 0's stack in `SCRATCH_Y`, a 4 KiB region on
the RP2350, and a larger value fails to link. That leaves about 400 bytes for
the main loop's and the handler's own frames, so keep a handler that logs
shallow.

```cmake
target_compile_definitions(app PRIVATE PICO_STACK_SIZE=0x1000)
```

**A slot is sized for the longest line, not for yours.** `capacity()` is a
count of slots, and each one holds `LOG_LINE_SIZE` bytes — the full header
reservation plus the full message buffer plus the newline — and its length.
Pick the capacity from the RAM you can spare and the burst you need to absorb.

**What a `RingSink` does not remove from the handler.** It takes `std::fwrite`
out of it, and that is all it takes out. The line's header — timestamp
included — is assembled before the sink is called, so the clock read,
`localtime_r` and `strftime` still run in your handler. The last two call
newlib's `__tz_lock`, which is an empty function unless a port retargets
newlib's locks; in an image that does, a FreeRTOS build typically, it is a real
lock and the deadlock above is back (`CR-5`). `tests/device/measurements.md`
carries the commands to check your own toolchain and SDK.

## Version constants

`pico_logging::VERSION_MAJOR`, `VERSION_MINOR` and `VERSION_PATCH` are the
version of the headers you compiled against. They equal the version the build
system declares: the library's own `static_assert` in `sinks.cpp` and its
`tests/core` suite hold the two together, so a consumer does not need to. Check
them against the major your code was written for, so a build that resolves a
different major fails at compile time rather than at the first changed
interface:

<!-- example: guide_version -->
```cpp
static_assert(pico_logging::VERSION_MAJOR == 0 &&
                  pico_logging::VERSION_MINOR == 1,
              "written against pico_logging 0.1.x");
```

The literal is the point: it is the consumer's statement of what it depends on.
