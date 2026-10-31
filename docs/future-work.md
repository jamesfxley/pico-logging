# Future work

Work deliberately not done, one entry per deferral, in the form `EN-03.7`
defines.

## FW-1 — RP2040 build

### What is absent

The library builds only for RP2350 boards. A firmware build for an RP2040 board
such as the Pico W is refused at configure (AR-6), and no CI leg builds it.

### Why it is not built

CR-1 asserts `std::atomic<T>::is_always_lock_free` for the current sink and the
runtime level. GCC reports false for every type on ARMv6-M (Cortex-M0+), which
has no exclusive-access instructions, so a firmware build for an RP2040 board
fails at `src/logging/sinks.cpp`; the library therefore declares
`PLATFORM rp2350`. Aligned 32-bit loads and stores exist on ARMv6-M, and only
read-modify-write does not. No consumer has needed an RP2040 build, so
re-specifying CR-1 to the operations the log path performs has not been
prioritized.

### What would have to be true to build it

A consumer that needs an RP2040 build. Then CR-1 is re-specified to the
operations the log path performs, and the assertion is split per architecture,
as pico-util's publication protocol already does. Then a `pico_w` firmware
leg in CI builds pico-logging, pico-time and pico-http, and `PLATFORM rp2350`
is dropped from all three.

### Version cost

Not breaking under `EN-06.1`: it adds a platform.

### Affected repositories

pico-logging, pico-time, pico-http and pico-bno085. Each of the other three
has a partner entry that cites this one. pico-time and pico-http link
pico-logging and are refused on the RP2040 through it. pico-bno085 also
declares `PLATFORM rp2350`.

## FW-2 — Per-module log levels

### What is absent

There is one runtime level and one compile-time ceiling, both global. A
consumer cannot turn debug logging on for one subsystem while leaving the rest
at info.

### Why it is not built

The natural implementation, a compile-time tag per translation unit with a
per-tag ceiling, collides with the ODR constraint that shapes the current
design. The ceiling is a macro so that it never becomes a linked entity, and a
per-module scheme has to store the modules somewhere, which is a linked entity
whose value differs between translation units.

It is not impossible: a tag type carrying the level as a template parameter
would work. It changes every call site's expansion, so it waits.

### What would have to be true to build it

A consumer that cannot work without it. Until then the cost, every call site's
expansion changing, buys nothing that raising the global level does not.

### Version cost

Not breaking under `EN-06.1` if the tag defaults to the current global
behaviour, so existing call sites are unchanged. Breaking if the macro's shape
changes.

### Affected repositories

pico-logging.

## FW-3 — 0.1.0 device measurements are not recorded

### What is absent

The device run on 0.1.0 that settles the second half of AR-2 (the log path
through `StderrSink` does not reach the allocator), AR-5's stack depth and
CR-4's masked interval. `tests/device/measurements.md` records no run on 0.1.0,
so these three are unverified for 0.1.0.

### Why it is not built

The run needs a device session, and none has been held for 0.1.0. AR-2's second
half is settled only by measurement on device, which the specification does not
allow newlib's sources to replace, and AR-5 and CR-4 include code this library
does not own.

### What would have to be true to build it

A device session on an RP2350 board in which the AR-2 rig runs in its one
configuration, stdio over USB, and the AR-5 and CR-4 rig runs for `Release`,
`Debug` and `MinSizeRel` with stdio over USB and over USB and UART. Each
result is recorded in `tests/device/measurements.md` with its conditions.
The statements that mark these results pending then say what was measured.

### Version cost

Not breaking under `EN-06.1`: it adds a run record and changes no interface.

### Affected repositories

pico-logging.
