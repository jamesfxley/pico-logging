# pico-logging Architecture

This document describes how the implementation is put together. It is not
normative; [technical-specification.md](technical-specification.md) states what
the library guarantees.

## Shape

The library has one compiled translation unit, in `pico_logging::logging`, which
the INTERFACE umbrella `pico_logging::core` carries. `pico_logging::testing` is
a header-only module beside it, built on the host only. Almost everything is in
headers because the statement macros expand at the call site, the line buffer is
a temporary the caller constructs, and the filtering that matters happens before
any call is made.

| File | Holds |
|---|---|
| `logging.hpp` | The `PICO_LOG_*` macros, `Level`, `LogLine` and its insertion operators, numeric dispatch and float formatting, the level controls |
| `sinks.hpp` | The `Sink` interface and `StderrSink` |
| `ring_sink.hpp` | `RingSink`, the IRQ-safe queue |
| `pico_logging.hpp` | Umbrella header and the advertised version constants |
| `src/logging/sinks.cpp` | The only compiled TU: `StderrSink`, the installed-sink pointer, the runtime level and the shared integer formatter |

## Statement path

`PICO_LOG_INFO << "x=" << x;` expands to an `if constexpr` on the compile-time
ceiling wrapping the construction of a `LogLine` temporary. The constructor
reads the runtime level once and stores the answer, so each `<<` on a disabled
line is a branch on that flag. Formatting accumulates into the temporary's own
array, and the destructor emits it.

Three properties follow, and each is a requirement:

- The line is emitted in exactly one `write()`, then one `flush()`. Several
  calls per line would let two contexts logging concurrently interleave halves
  into a sequence no sink could reassemble. One call is also one record, which
  is what makes a queueing sink possible: `RingSink` never has to put a line
  back together.
- The buffer is fixed and lives in the temporary: `LOG_HEADER_SIZE +
  LOG_BUFFER_CAPACITY + 1` bytes on the stack of whoever logs. An overlong
  message is cut at the buffer behind a `...` marker, and an overlong file name
  is cut from the front behind one, so the line number always lands. Neither
  grows into the heap.
- Nothing on the path allocates. Integers go through `std::to_chars`, and the
  header and timestamp are assembled by appending parts. There is no `snprintf`,
  because newlib's `%g` reaches `_dtoa_r` → `_Balloc` → `_malloc_r`.

## Filtering

The compile-time ceiling is the macro `PICO_LOGGING_EFFECTIVE_LEVEL`, read only
by the `if constexpr` inside each macro. Above the ceiling the whole statement
is discarded, and its operands are never evaluated.

The runtime level is a variable in `sinks.cpp`, set by `set_level()`. Below the
ceiling but above the runtime level, the statement is constructed and its
operands are evaluated; only the emission is skipped.

The ceiling is a macro and not a constant because its value varies per
translation unit, so it must never reach a linked entity. Moving it into an
`inline constexpr` variable, an `inline` function or a template, as
`cppcoreguidelines-macro-usage` suggests, makes its value part of a linked
symbol. Two translation units compiled at different ceilings then violate the
ODR, and logging silently appears or disappears depending on link order and
optimisation level. `tests/logging/level_odr/` links exactly that pair and is
the behavioural regression. `level_ceiling_is_macro_only.py` fails on the forms
the optimiser folds, which that pair cannot see.

The levels are named `ERR` and `DBG`, not `ERROR` and `DEBUG`, because SDK
headers and build flags already define macros of those names.

`set_level()` stores what it is given and caps nothing. A cap would be the
ceiling `sinks.cpp` itself was compiled at, which for an installed package is
the default, and it would put `Level::TRACE` out of every consumer's reach.

## Floating point

`detail::format_float` renders doubles instead of `std::to_chars`, for a
measured reason. libstdc++'s shortest-round-trip path calls Ryu's
fixed-notation renderer, so four lookup tables are reachable and survive
`--gc-sections`, and one logged `double` grows the cross probe several-fold. The
hand formatter costs a fraction of that. The figures and their conditions are in
[tests/firmware/measurements.md](../tests/firmware/measurements.md).

The price is a restricted domain, which the specification states: correctly
rounded to six places inside `[1e-4, 1e9)`, and approximate exponent notation
outside it. A non-zero magnitude below `1e-4` takes exponent notation because
six fixed decimal places keep at most two of its significant digits, and none
below `5e-7`. The formatter also needs IEEE
semantics. A consumer's fast or unsafe math flags remove them, and the
formatter is inline, so it compiles under those flags. Logging a float
therefore fails to compile under each flag the compiler names. NaN and infinity
are classified by bit pattern, which no flag changes.

Extending the correctly rounded range means extending the exact-product
recovery that makes it correct, and each extra decade costs precision headroom
in a `double` multiplication that already carries a residual correction. The
specification states the domain exactly, so a consumer knows where it stops.

Inside that range the rounding decision is made on the exact product.
`magnitude * FLOAT_DECIMAL_SCALE` is a `double` multiplication, and above 1e5
its own rounding is enough to carry the last decimal place the wrong way, so
`detail::product_residual` recovers the part the multiplication dropped. The
nearby `static_assert` bounds the result of that multiplication, not the
multiplication itself. It does not cover the rounding error.

## Sinks

`Sink` is a two-method interface: `write()` for one whole line, `flush()` after
it. The installer keeps ownership, and the sink must outlive the installation.
`set_sink(nullptr)` restores `StderrSink`.

`StderrSink` uses `std::fwrite`, which is not safe from interrupt context. Where
newlib is built with FILE locking, a handler that pre-empts a mainline caller
holding that FILE's lock blocks on a lock only the interrupted code can release.

`RingSink<Slots, Lock>` is the supported route out of a handler. It is a queue
of fixed-size slots, written by every context that logs and drained by one. A
producer takes the caller-supplied lock, copies its line into a slot and
returns. The main loop calls `drain()` to forward whatever accumulated to a
target sink. Installed with `set_sink`, the ring has as many producers as the
image has contexts that log, so the lock is a template parameter the caller must
supply and not an option. On the RP2350 the SDK's `critical_section` is that
lock; the library itself contains no platform code. The ring underneath is
pico-util's `RingBuffer` with `SpscCounter`: the lock makes the producers one
writer, so the single-producer counter stays correct.

A line longer than a slot is dropped whole and counted, not cut, and a full ring
drops instead of blocking. `drain()` takes no lock. Two consumers are neither
supported nor detected.

A slot holds a whole line, so the queue costs `Slots × sizeof(Slot)` and not
the length of the lines actually queued. That is why the capacity is a template
parameter and not a runtime argument, and why a large capacity is not a modest
default.

`RingSink` removes `std::fwrite` from the handler and nothing more. The header
is assembled before the sink is called, so the clock read, `localtime_r` and
`strftime` run in the producer's context on every statement. The last two take
newlib's TZ lock, which is an empty function unless a port retargets newlib's
locking (CR-5).

## Timestamps

Timestamps come from `std::chrono::system_clock`. On firmware that counts from
boot unless something, typically pico-time, has synchronised the wall clock. The
library does not depend on which.

## Allocation checks

AR-1 is that the log path never allocates, and no single gate establishes it.
The host counter in `allocation_test.cpp` replaces the global allocation
functions, so it sees a `new` but not newlib's internal `_malloc_r`. The
cross-compiled probe in `tests/firmware/` is therefore inspected as an image by
the `symbols` gate, which looks for `_dtoa_r` and `_Balloc`, the allocating
formatter itself, and not for `malloc`. The allocator is linked into any image
that uses stdio (AR-2), so its presence says nothing about the log path. The
formatter is present only if something on the path reaches it.

The image question and the object question differ, so the probe is inspected
both ways. As an image it answers what was linked in. As objects, with the
`c-heap-alloc` set, it answers what this library's own code calls, which is
where `format_timestamp` and the inline parts of `LogLine` land. The same set
inspects `sinks.cpp.o`, which owns the integer formatter. The set covers only
the allocating half: `Sink` is polymorphic, and a vtable's deleting-destructor
slot names `operator delete` in every translation unit that defines a sink,
whether or not anything allocates.

Each gate has a negative control that it must reject, run on every push to main
and every pull request (AR-1).

That `StderrSink` links an allocator is not in dispute. AR-2 is the narrower
claim that the log path does not reach it, which depends on the SDK's default
stdio buffering and not on the sink. A device run settles it, as
[examples/bench/measurements.md](../examples/bench/measurements.md) describes;
the run on 0.1.0 is pending (FW-3). The claim is re-measured, not assumed, if
the libc or its buffering changes.

## Line format

Every statement produces one text line: timestamp, level, source location,
message. The message text is escaped on the way in, a control byte as an escape
and a backslash doubled, so nothing a caller streams can end the line or forge
another. A line cut at capacity keeps an escape whole or drops it. The library
cannot emit key/value pairs, JSON or anything a machine would parse without a
regex.

Structured records would need a second encoder, a second buffer discipline and a
`Sink` interface that carries fields and not a `std::string_view`. The field set
would then be part of the public API, which is the expensive part to walk back.
The library's cost model rests on one fixed buffer per statement and one
`write()` per line, and a field-carrying sink either allocates or imposes a
field ceiling that is another number to get wrong.

A consumer that needs machine-readable output formats it into the message
itself. That costs the consumer effort, but the encoding decision stays with the
consumer.

## Compile-time checking

Insertion is dispatched by type trait, and a type with no inserter renders as
`<?>` and does not fail to compile, so adding a field to a logged struct cannot
break a build. Nothing checks the content of a message: there is no format
string to validate, and no way to express that a line must always carry certain
fields.

`std::format`-style checking would mean adopting a format string, and with it
format-string parsing on the log path. The varargs form is banned, and the
printf rule in `src/.clang-tidy` is the gate.

## Out of scope

Spans, sampling, correlation IDs and anything else with a lifetime longer than
one statement are out of scope and not merely unbuilt. The unit here is a line
that is complete when the statement ends. A construct that outlives its
statement needs storage this library does not have and a lifetime contract it
does not want to enforce.
