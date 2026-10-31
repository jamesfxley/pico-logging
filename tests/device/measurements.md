# AR-2: does the log path through `StderrSink` allocate?

Observations, not requirements. The specification states gates; the numbers a
gate was set from live here, with the conditions that produced them
(`EN-03.2`, `EN-06.7`). Nothing here is normative, and a stale figure is a
stale figure rather than a broken promise.

Re-measure by rebuilding the run below. A number without its build type and
toolchain cannot be compared against anything.

The rig is `ar2_heap_watermark.cpp` beside this file; `README.md` says how
to build, flash and read it.

AR-2 records that an image linking `StderrSink` links an allocator, and that
under the SDK's default stdio the log path does not reach it. That second half
turns on the stream's buffering mode, which the specification forbids settling
by reading newlib's sources. This is the device run that settles it.

**Status: no run on 0.1.0 is recorded (FW-3).** Until it is recorded, the
second half of AR-2 is unverified for 0.1.0.

A run records these conditions: date, the pico-logging and pico-util revisions,
the board and `clk_sys`, the Pico SDK version and C library type, the toolchain
and newlib versions, the build type, the stdio routing, the sink (the default:
nothing calls `set_sink`) and the rig, built as `README.md` says.

Instrument: `-Wl,--wrap=_malloc_r`, counting calls and bytes. `_malloc_r` is
the function `__smakebuf_r` calls, so the count is of the exact event in
question; the SDK wraps `malloc`, a different symbol, so the two coexist.
`mallinfo().uordblks` and the program break are recorded alongside as secondary
evidence.

The rig reports once a second. A run records the first report captured whole,
with these samples: a baseline before any log, after one `StderrSink` line,
after a 500-line burst, and after a deliberate `malloc(1024)`. The last is the
instrument's negative control and is what makes the first three mean anything:
a deliberate allocation has to move every column, so a stationary counter is a
stationary heap rather than a dead counter.

A result is a property of its configuration, not of `StderrSink` in the
abstract: a build whose `stderr` is fully buffered would allocate a buffer on
first write.

## The image inspection

AR-2's first half: linking `StderrSink` links newlib stdio, and the stream
set-up it brings calls the allocator. Settled without a board, by reading a
linked image.

The image is the cross-compiled log probe. It isolates `StderrSink`: it logs
only through a `RingSink` draining to a sink that touches no stdio, and it
calls no `printf`, so `StderrSink` is linked only as the default sink and is
the only route by which stdio can arrive. Both device rigs beside this record
call `printf` and cannot separate the two.

| Condition | Value |
|---|---|
| Date | 2026-10-29 |
| pico-logging | 0.1.0, the tree of the commit that adds this record |
| pico-util | 0.1.0 at `604e9fc2`, fetched through the pinned dependency |
| Pico SDK | 2.3.0 |
| Toolchain | `arm-none-eabi-gcc` 14.2.1 (`14.2.rel1-1`, newlib 4.6.0.20260123) |
| Build type | `Release`, `PICO_BOARD=pico2_w` |
| Image | `build-firmware/tests/firmware/pico_logging_log_probe.elf`, from `python3 dev.py build --tree firmware` |

```sh
ELF=build-firmware/tests/firmware/pico_logging_log_probe.elf
arm-none-eabi-nm -C "$ELF" | grep -c StderrSink
arm-none-eabi-nm "$ELF" | grep -E ' (__swsetup_r|__smakebuf_r|_malloc_r|printf)$'
arm-none-eabi-objdump -d --no-show-raw-insn "$ELF" |
    awk '/<__smakebuf_r>:/ {p = 1} p && /^$/ {exit} p' | grep -E 'bl\s'
```

| Step | Result |
|---|---|
| `StderrSink` symbols linked | yes |
| `printf` linked | no |
| `__swsetup_r` present | yes |
| `__smakebuf_r` present | yes |
| `__smakebuf_r` calls | `_fstat_r`, `_malloc_r` twice, `_isatty_r` |

**Result: an image linking `StderrSink`, and logging through a different sink,
links `__swsetup_r` and `__smakebuf_r`, and `__smakebuf_r` calls `_malloc_r`.**

---

# AR-5 and CR-4: what a log statement costs the context it runs in

Observations, not requirements, on the same terms as the section above.

AR-5 bounds how deep into the stack a log statement reaches, and CR-4's lock is
held with interrupts masked. Both are figures a consumer sizes an image from,
and both include code this library does not own — newlib's time calls, the
SDK's `critical_section` — so they are measured on the board rather than read
off this library's own frames.

**Status: no run on 0.1.0 is recorded (FW-3).** Until it is recorded, AR-5's
depth and CR-4's masked interval are unverified for 0.1.0.

A run covers `Release`, `Debug` and `MinSizeRel`, each with stdio over USB and
with stdio over USB and UART, and records the date, the pico-logging and
pico-util revisions, the board and `clk_sys`, the Pico SDK version and C library
type, and the toolchain. The rig is `tests/device/cr4_ring_sink_costs.cpp`,
built as `README.md` says for each build type and stdio routing. Its sinks are
`RingSink<4, TimedCriticalSection>` over a counting sink, the lock the SDK
`critical_section` with SysTick read inside it, measured with interrupts
masked; then the default `StderrSink`, newlib stdio into the SDK's stdio
drivers, measured with every interrupt but USB's disabled so the USB driver has
transfer events to dispatch.

Stack depth is how far below the measuring function's stack pointer the
deepest word no longer holding the paint pattern lies. It is what the statement
touched, so it is a lower bound on what it reserved; through `StderrSink` it can
also include the USB interrupt handler's frames, which overstates it. The
statements measured are the first in the image (an `int`, which initialises
newlib's time-zone state), an `int` and a fixed-notation `double`, an
exponent-notation `double`, a `string_view` with a `bool` and a `char`, and a
message cut at capacity, through each sink, with a 512-byte frame as the
control.

The lock hold is the masked interval of `RingSink::write` for a 513-byte line,
the longest a statement can produce, worst of 1,000, with a 10 µs busy-wait
under the lock as the control.

Through `StderrSink` the USB driver's event dispatch, or the USB handler, can go
deeper than the statement, but only when a transfer completes during it, so the
depth varies from run to run; AR-5's bound has to cover the deepest statement
plus that excursion. Stdio over RTT or semihosting is not covered.

The rig counts one dropped line in each run: the five `RingSink` statements run
with interrupts masked and nothing draining, into a four-slot ring.

---

# CR-5: does the timestamp path take a lock?

Observations, not requirements, on the same terms as the section above.

CR-4 says the producer may be an interrupt handler. Everything before
`sink->write()` runs in that context, and `format_timestamp` is the first thing
on it — so the question CR-3 asks of `std::fwrite` has to be asked of
`localtime_r` and `strftime` too.

Unlike AR-2 this is settled without a board. The answer is a property of the
libc that gets linked, and it is visible in the toolchain's own archives, which
is admissible where reading newlib's sources is not. It is also the more useful
form: it can be re-derived for any toolchain in a few seconds, where a device
run answers for one board and one SDK.

| Condition | Value |
|---|---|
| Date | 2026-10-29 |
| Toolchain | `arm-none-eabi-gcc` 14.2.1 (`14.2.rel1-1`), newlib 4.6.0.20260123 |
| Pico SDK | 2.3.0 |
| Multilib | `thumb/v8-m.main+fp/softfp` (RP2350) |
| Linked image | `build-device/cr4_ring_sink_costs.elf`, `Release`, at the 0.1.0 tree of the commit that adds this record, with pico-util 0.1.0 at `604e9fc2` |

The archive steps inspect the toolchain's own `libg.a` and do not depend on a
build type. The linked-image steps inspect the CR-4 rig above, which is the
configuration a consumer logging through a `RingSink` builds.

## Re-deriving it

Which libc objects reference the TZ lock:

```sh
LIB=/usr/lib/arm-none-eabi/newlib/thumb/v8-m.main+fp/softfp/libg.a
arm-none-eabi-nm -A "$LIB" | grep -E 'lcltime_r|strftime' | grep __tz_lock
```

What `__tz_lock` and the clock read resolve to once linked:

```sh
ELF=build-device/cr4_ring_sink_costs.elf
arm-none-eabi-nm "$ELF" | grep -E ' (__tz_lock|__retarget_lock_acquire|_gettimeofday|time_us_64)$'
arm-none-eabi-objdump -d --start-address=<addr> --stop-address=<addr+0x30> "$ELF"
```

And, for a newlib built with retargetable locking, what the port supplies:

```sh
arm-none-eabi-ar x "$LIB" libc_a-tzlock.o
arm-none-eabi-objdump -d libc_a-tzlock.o
arm-none-eabi-ar x "$LIB" libc_a-lock.o
arm-none-eabi-objdump -d libc_a-lock.o
```

## Result

| Step | Result |
|---|---|
| `localtime_r` (`libc_a-lcltime_r.o`) references `__tz_lock` | yes |
| `strftime` (`libc_a-strftime.o`) references `__tz_lock` | yes |
| `__tz_lock` body, in the archive and in the linked image | `bx lr` |
| `__retarget_lock_acquire` in the archive | not present: this newlib is not built with retargetable locking |

The clock read, from the same linked image: `_gettimeofday` is the SDK's, and
calls `time_us_64` — two reads of the timer's high word around one of its low
word, retried until the high word agrees — then a 64-bit division helper. It
takes no lock and calls nothing else.

**Result: the timestamp path takes newlib's TZ lock in the call graph, and that
lock is an empty function in this toolchain; the SDK 2.3.0 clock read takes no
lock.** The hazard CR-3
describes is therefore not reachable through `format_timestamp` in these
builds, and becomes reachable the moment a port supplies a real
`__retarget_lock_*` — which is what a FreeRTOS build does.

Two further observations from the same inspection, recorded because they bear
on AR-1 rather than on CR-5:

- `_tzset_unlocked_r` references `_malloc_r` and `free`. The allocator is in
  the timestamp path's link graph, whether or not it is reached — which is why
  AR-1 cites the AR-2 burst above rather than resting on the symbol gates.
- `strftime` references `sniprintf`, newlib's integer-only formatter, the
  non-allocating one.
