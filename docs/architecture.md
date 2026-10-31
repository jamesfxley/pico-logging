# pico-logging Architecture

How the library is put together. What it guarantees is
[technical-specification.md](technical-specification.md); this file describes
the implementation and is not normative.

## Shape

One compiled translation unit, in `pico_logging::logging`, which the INTERFACE
umbrella `pico_logging::core` carries; `pico_logging::testing` is a header-only
module beside it, built on the host only. Almost
everything is in headers because almost everything has to be: the statement
macros expand at the call site, the line buffer is a temporary the caller
constructs, and the filtering that matters happens before any call is made.

| File | Holds |
|---|---|
| `logging.hpp` | The `PICO_LOG_*` macros, `Level`, `LogLine` and its insertion operators, numeric dispatch and float formatting, the level controls |
| `sinks.hpp` | The `Sink` interface and `StderrSink` |
| `ring_sink.hpp` | `RingSink`, the IRQ-safe queue |
| `pico_logging.hpp` | Umbrella header and the advertised version constants |
| `src/logging/sinks.cpp` | The only compiled TU: `StderrSink`, the installed-sink pointer, the runtime level and the shared integer formatter |

## A statement's life

`PICO_LOG_INFO << "x=" << x;` expands to an `if constexpr` on the compile-time
ceiling wrapping the construction of a `LogLine` temporary. The constructor
reads the runtime level once and stores the answer, so each `<<` on a disabled
line is a branch on that flag. Formatting accumulates into the temporary's own
array; the destructor emits it.

Three things follow from that arrangement, and each is a requirement rather
than an accident:

- **The line is emitted in exactly one `write()`, then one `flush()`.**
  Issuing several calls per line would let two contexts logging concurrently
  interleave halves into a sequence no sink could reassemble. This is also what
  makes a queueing sink possible at all: one call is one record, so `RingSink`
  never has to put a line back together.
- **The buffer is fixed and lives in the temporary.** `LOG_HEADER_SIZE +
  LOG_BUFFER_CAPACITY + 1` bytes on the stack of whoever logs. An overlong
  message is cut at the buffer behind a `...` marker, and an overlong file name
  is cut from the front behind one, so the line number always lands; neither
  grows into the heap.
- **Nothing on the path allocates.** Integers go through `std::to_chars`; the
  header and timestamp are assembled by appending parts. There is no `snprintf`
  anywhere, because newlib's `%g` reaches `_dtoa_r` → `_Balloc` → `_malloc_r`.

## Filtering happens twice, and the two differ in cost

The compile-time ceiling is the macro `PICO_LOGGING_EFFECTIVE_LEVEL`, read only
by the `if constexpr` inside each macro. Above the ceiling the whole statement
is discarded, operands included — the arguments are never evaluated.

The runtime level is a variable in `sinks.cpp`, set by `set_level()`. Below the
ceiling but above the runtime level, the statement *is* constructed and the
operands *are* evaluated; only the emission is skipped.

**The ceiling must never reach a linked entity.** Moving it into an
`inline constexpr` variable, an `inline` function or a template — which is what
`cppcoreguidelines-macro-usage` will suggest — makes its value part of a linked
symbol. Two translation units compiled at different ceilings then violate the
ODR, and the symptom is logging silently appearing or disappearing depending on
link order and optimisation level. `tests/logging/level_odr/` links exactly that pair
and is the behavioural regression; `level_ceiling_is_macro_only.py` fails on the
forms the optimiser folds, which that pair cannot see.

`set_level()` stores what it is given and is capped by nothing. A cap would be
the ceiling `sinks.cpp` itself was compiled at, which for an installed package
is the default, and would put `Level::TRACE` out of every consumer's reach.

## Floating point

`detail::format_float` renders doubles instead of `std::to_chars`, and the
reason is measured rather than stylistic. libstdc++'s shortest-round-trip path
calls Ryu's fixed-notation renderer, so four lookup tables are reachable and
survive `--gc-sections`, and one logged `double` grows the cross probe
several-fold. The hand formatter costs a fraction of that; the figures and their
conditions are in
[tests/firmware/measurements.md](../tests/firmware/measurements.md).

The price is a restricted domain, and the specification states it: correctly
rounded to six places inside `[1e-4, 1e9)`, approximate exponent notation
outside it. It also needs IEEE semantics, which a consumer's fast or unsafe
math flags remove, since the formatter is inline and compiles under them; so
logging a float is refused at compile time under each flag the compiler names,
and NaN and infinity are classified by bit pattern, which no flag changes.

Extending the correctly rounded range means extending the exact-product
recovery that makes it correct, and each extra decade costs precision headroom
in a `double` multiplication that is already carrying a residual correction.
The domain is stated in the specification rather than approximated, so a
consumer knows exactly where it stops.

Inside that range the rounding decision is made on the exact product.
`magnitude * FLOAT_DECIMAL_SCALE` is a `double` multiplication, and above 1e5
its own rounding is enough to carry the last decimal place the wrong way, so
`detail::product_residual` recovers the part the multiplication dropped. The
`static_assert` nearby bounds the *result* of that multiplication, not the
multiplication itself, and reads as though it covers this — it does not.

## Sinks

`Sink` is a two-method interface: `write()` for one whole line, `flush()`
after. The installer keeps ownership and must outlive the installation;
`set_sink(nullptr)` restores `StderrSink`.

`StderrSink` uses `std::fwrite`, which is **not** safe from interrupt context:
where newlib is built with FILE locking, a handler that pre-empts a mainline
caller holding that FILE's lock blocks on a lock only the interrupted code can
release.

`RingSink<Slots, Lock>` is the supported route out of a handler. It is a queue
of fixed-size slots written by every context that logs and drained by one: a
producer takes the caller-supplied lock, copies its line into a slot and
returns, and the main loop calls `drain()` to forward whatever accumulated to a
target sink. Installed with `set_sink`, it has as many producers as the image
has contexts that log, which is why the lock is a template parameter the caller
must supply rather than an option. On the RP2350 the SDK's `critical_section`
is that lock; the library itself contains no platform code. The ring
underneath is pico-util's `RingBuffer` with `SpscCounter`: the lock makes the
producers one writer, so the single-producer counter stays correct.

A line longer than a slot is dropped whole and counted rather than cut, and a
full ring drops rather than blocks. `drain()` takes no lock, and two consumers
is neither supported nor detected.

A slot holds a whole line, so the queue costs `Slots × sizeof(Slot)` rather
than the length of the lines actually queued. That is why the capacity is a
template parameter rather than a runtime argument, and why a large capacity is
not a modest default.

What it removes from the handler is `std::fwrite`, and no more than that. The
header is assembled before the sink is called, so the clock read, `localtime_r`
and `strftime` run in the producer's context on every statement; the last two
take newlib's TZ lock, which is an empty function unless a port retargets
newlib's locking (CR-5).

## Timestamps

Timestamps come from `std::chrono::system_clock`. On firmware that counts from
boot unless something — pico-time, typically — has synchronized the wall clock,
and this library neither knows nor cares which.

## Allocation, and why one gate is not enough

AR-1 is that the log path never allocates, and one gate cannot establish it.
The host counter in `allocation_test.cpp` replaces the global allocation
functions, which sees a `new` but cannot see newlib's internal `_malloc_r`.
So the cross-compiled probe in `tests/firmware/` is inspected as an image, by
the `symbols` gate, looking for `_dtoa_r`/`_Balloc` — the
allocating formatter itself — rather than for `malloc`. The allocator is linked
into any image that uses stdio (AR-2), so its presence says nothing about the
log path; the formatter is present only if something on the path reaches it.

The image question and the object question are not the same one, so the probe
is inspected both ways. As an image it answers what was linked in; as objects,
with the `c-heap-alloc` set, it answers what this library's own code calls —
which is where `format_timestamp` and the inline parts of `LogLine` land.
The same set inspects `sinks.cpp.o`, which owns the integer formatter.
The set is the allocating half only:
`Sink` is polymorphic, and a vtable's deleting-destructor slot names
`operator delete` in every translation unit that defines a sink, allocation or
no allocation.

Each gate has a negative control that must be rejected, run on every push to
main and every pull request. A gate that has only ever been seen to pass is not
evidence.

That `StderrSink` links an allocator at all is not in dispute; AR-2 is the
narrower claim that the log path does not *reach* it, which is a property of
the SDK's default stdio buffering rather than of the sink. It is settled by a
device run, described in
[tests/device/measurements.md](../tests/device/measurements.md), whose run on
0.1.0 is pending (FW-3), and re-measured rather than assumed if the libc or its
buffering changes.

## One text line, and no structured form

Every statement produces one text line: timestamp, level, source location,
message. The message's text is escaped on the way in, a control byte as an
escape and a backslash doubled, so nothing a caller streams can end the line or
forge another; a line cut at capacity keeps an escape whole or drops it. There
is no way to emit key/value pairs, JSON, or anything a machine would parse
without a regex.

Structured records would need a second encoder, a second buffer discipline, and
a `Sink` interface that carries fields rather than a `std::string_view` — and
the field set would then be part of the public API, which is the expensive part
to walk back. The library's whole cost model rests on one fixed buffer per
statement and one `write()` per line; a field-carrying sink either allocates or
imposes a field ceiling that is another number to get wrong.

A consumer that needs machine-readable output formats it into the message
itself. That is not free, but it is honest about where the encoding decision
lives.

## What is checked at compile time, and what is not

Insertion is dispatched by type trait, and a type with no inserter renders as
`<?>` rather than failing to compile, so adding a field to a logged struct
cannot break a build. What is not checked is anything about the *content*
of a message — there is no format string to validate, and equally no way to
express "this line must always carry these fields".

`std::format`-style checking would mean adopting a format string, and with it
format-string parsing on the log path. The varargs form is banned outright,
and the printf rule in `src/.clang-tidy` is the gate.

## What this is not

Spans, sampling, correlation IDs and anything else with a lifetime longer than
one statement are out of scope rather than unbuilt. The unit here is a line
that is complete when the statement ends; a construct that outlives its
statement needs storage this library does not have and a lifetime contract it
does not want to enforce.
