# pico-logging Technical Specification

**Status:** Normative.

This specification defines the architecture, formatting, boundary, concurrency
and observability requirements for the pico-logging library on the Raspberry Pi
Pico family and on the host. Every requirement has an ID (`AR`/`FR`/`BR`/`CR`/
`OR`) and is mapped to an acceptance test in the traceability matrix at the end of this document.

Requirement IDs are cited from the code and tests that implement and exercise
them (`EN-02.1`). Cite the ID, never a section number (`EN-02.2`); IDs are never
reused or renumbered (`EN-02.3`).

---

## 1. Scope and targets

| | |
|---|---|
| Targets | The host (GCC/Clang, hosted libc) and RP2350 freestanding cross builds. The same translation units serve both; there is no firmware-conditional code. An RP2040 board is refused at configure: CR-1's atomics are not lock-free on ARMv6-M. |
| Language | C++20. |
| Public surface | `namespace pico_logging`, headers under the `pico_logging/` prefix. Three consumer-facing targets: `pico_logging::core`, the INTERFACE umbrella a consumer links; `pico_logging::logging`, the compiled module it carries, linkable alone; and `pico_logging::testing`, the header-only capture sink a consumer's own suite asserts against, which is host-only and not created in a firmware build. `pico_logging_strict` is exported as an artefact of `install(EXPORT)` and is not one a consumer links. |
| Out of scope | Log routing, rotation, filtering by category or subsystem, and any persistence. A `Sink` is the seam where those belong, and implementing one is the consumer's job. |

**API stability:** semantic versioning; breaking changes only at a major
version.

The library is used on a dual-core RP2350 alongside lwIP, whose callbacks run in
a background/interrupt context. The concurrency requirements (CR) are written
for that setting.

---

## 2. Architecture requirements (AR)

- **AR-1 — The log path allocates nothing.** From the construction of a
  `LogLine` to the return of `Sink::write`, the library performs no dynamic
  allocation. The line is staged in a fixed-capacity buffer held by the
  statement's own temporary; integers are formatted with `std::to_chars` and
  floating point with `detail::format_float` (FR-5), both into fixed stack
  scratch.

  `snprintf` must not appear on the log path. On newlib the `%g` conversion
  reaches `_dtoa_r` → `_Balloc` → `_malloc_r`, which allocates whenever the
  bucket freelist is empty — on the log path of a long-running image, which is
  the fragmentation this requirement exists to prevent (`CXX-11.1`).

  Verified five ways, because each check misses what the others catch:

  1. A host allocation counter over every inserter branch. It cannot see
     newlib's allocating formatter — glibc's `%g` does not allocate — so it
     establishes the library's own behaviour and nothing about newlib.
  2. A cross-compiled image inspected for the allocating formatter by
     the `symbols` gate (`python3 dev.py gate symbols`). This is the authoritative check for newlib's
     allocating formatter, and the only one of the five that can see it. Its
     scope is that formatter and nothing else: it matches `_dtoa_r`, `_dtoa`, `_Balloc`
     and `_ldtoa_r`, so no other route to the allocator moves it.
  3. The probe's objects and the compiled logging module's objects inspected
     for the allocator itself, by the same gate's `c-heap-alloc` set. It asks
     what each translation unit leaves undefined, covering the header-inline
     `format_timestamp` and remaining `LogLine` code together with the integer
     formatter in `sinks.cpp.o`. The image check above cannot separate these
     from the rest of the link. It does not see through a libc call, and it is
     the allocating half only: a `Sink`
     implementation is polymorphic, and a vtable's deleting-destructor slot
     references `operator delete` whether or not anything allocates.
  4. The printf rule in `src/.clang-tidy` (`bugprone-unsafe-functions` with
     `CustomFunctions` set to the printf family), which the `lint` gate runs
     over `src/` and which fails on a printf-family call anywhere in it. It
     needs no cross toolchain, so it runs on every push to main, every pull
     request and a developer machine, and it reports a reintroduction at the
     line that caused it rather than as a symbol in a linked image.
  5. The AR-2 device run described in `tests/device/measurements.md`, which
     counts `_malloc_r` across a burst through the default sink and must
     observe none. No run on 0.1.0 is recorded yet, so this evidence is
     pending. Every one of those lines formatted a timestamp, so it is the only
     evidence here covering the libc calls `format_timestamp` makes:
     `_tzset_unlocked_r` references `_malloc_r` and `free`, so the allocator is
     in that path's link graph whether or not it is reached. It covers one
     configuration, one SDK version and mainline context, and nothing more.

  Each gate's refusal is shown, because a gate that has only ever been seen to
  pass is not evidence:

  - `AllocationTest.TheCounterObservesAnAllocation` asserts the host counter
    can see an allocation at all.
  - The firmware build compiles `tests/firmware/allocating_fixture.cpp` —
    deliberately wrong — and a firmware gate asserts the symbol check
    *rejects* it.
  - It compiles `tests/firmware/heap_call_fixture.cpp` for the object check,
    and a gate asserts the same. A separate fixture, because the two gates ask different
    questions: a translation unit reaching the float formatter leaves the
    formatter undefined rather than the allocator, so neither fixture stands in
    for the other.
  - `tests/varargs/printf_rule_control.py` runs the shipped `src/.clang-tidy`
    over `tests/varargs/varargs_call.cpp.in`, which does call `snprintf`, and
    asserts the rule reports it at its line with its reason.
  - That the footprint gate refuses an image over either budget is
    pico-tooling's guarantee, shown by its own suite; here it is a gate run
    against the probe.

  Static RAM follows pico-tooling:AR-11: ALLOC except equal-VMA/LMA
  READONLY+LOAD sections, `.heap` and `.stack*`.

  The firmware tree runs its host-side CMake/nm CTest (the declared mode is
  `build-test-install`); it executes no ARM program.

  The probe's budgets are <= 8 KiB of static RAM and <= 64 KiB of flash
  (all `LOAD` sections). Both halves are bounded because a formatting choice
  can move either: a lookup table lands in flash, a buffer promoted to file
  scope lands in RAM, and a gate that watches one half reports nothing about
  the other.

  A control asserts the *exact* rejecting status, not merely a non-zero one.
  Each gate reports "could not run" separately from "rejected", and a control
  that accepts either passes when a renamed image or an absent tool means it
  never ran — which is the failure the controls exist to catch.

- **AR-2 — An image linking `StderrSink` links an allocator, and under the
  Pico SDK's default stdio the log path does not reach it.**

  The first half is established by inspecting a cross-compiled image: linking
  `StderrSink` links newlib stdio; `__swsetup_r` and `__smakebuf_r` are
  present; `__smakebuf_r` calls `_malloc_r`. This holds even for an image that
  never logs, so it is a property of linking that sink rather than of the
  formatting path, which AR-1 covers.

  The second half is established by measurement on device, which is the only
  admissible way to settle it — not by reading newlib's sources. Counting calls
  to `_malloc_r` across a burst through the default sink gives zero, against a
  deliberate allocation in the same run that the counter must observe. The run
  and its negative control are described in `tests/device/measurements.md`; no
  run on 0.1.0 is recorded yet, so this half is pending for 0.1.0.

  **This is a property of a configuration, not of the sink.** It holds for the
  stdio a Pico SDK consumer gets by default; a build whose `stderr` is fully
  buffered allocates a buffer on first write. An image that must provably
  allocate nothing under *any* libc configuration installs a sink that does not
  use stdio.

- **AR-3 — Static storage is constant-initialised.** The module state (default
  sink, current sink, runtime level) is `constinit` at namespace scope, not a
  function-local static. A function-local static is constructed on first use
  behind a guard variable, so a first log from interrupt context either observes
  a half-constructed sink or re-enters `__cxa_guard_acquire` on a guard the
  mainline holds (`CXX-08.3`).

- **AR-4 — Storage in the logging module is fixed at compile time.** Every
  buffer in `pico_logging::logging` is a `std::array` whose extent is a named
  constant. `RingSink`'s capacity is a template parameter, so an image's queue
  footprint is visible in its type and fixed at link time.
  `pico_logging::testing` is test support and allocates freely (FR-9).

  A slot holds a whole line, so that footprint is `Slots × sizeof(Slot)` rather
  than `Slots ×` the length of a line actually logged, and `sizeof(Slot)` is
  `LOG_LINE_SIZE` plus the stored length, rounded up to the slot's alignment.
  `ring_sink.hpp` states that where the capacity is chosen, because a footprint
  visible in the type is visible only to a reader who multiplies.

- **AR-5 — A log statement reaches at most 1.75 KiB of stack below its caller.**
  Every buffer a statement uses is a fixed-size array on the logging context's
  own stack — the line buffer, the timestamp, the number scratch — and nothing
  on the path recurses, so the depth is a property of the build rather than of
  the message. A `Sink` implementation shall not log from `write()`: that call
  is made from inside a log statement, and logging there recurses without
  bound. The bound includes the libc calls the timestamp makes, which is
  why it is established by measurement on device rather than by static
  analysis of this library's own frames.

  A consumer sizes its stacks from it, through `RingSink` or through
  `StderrSink` with stdio over USB CDC or UART. An
  interrupt handler that logs while the main loop is inside a log statement
  puts two statements and an exception frame on the main stack: 3.5 KiB and up
  to 104 bytes. That exceeds the Pico SDK's default `PICO_STACK_SIZE` of 2 KiB
  and fits, with about 400 bytes to spare, within the 4 KiB that `SCRATCH_Y`,
  where the SDK places it, allows. The bound is verified by measurement on the
  RP2350; no run on 0.1.0 is recorded yet (`tests/device/measurements.md`).

- **AR-6 — A board without an RP2350 is refused at configure.** CR-1's atomics
  are lock-free only where the core has exclusive access, which ARMv6-M lacks,
  so the library declares `PLATFORM rp2350` to the family bootstrap, which
  refuses a board without what a library declares. A consumer adding it on an RP2040 board fails at configure, naming the
  platform, rather than at CR-1's `static_assert` in the build.

---

## 3. Functional requirements (FR) — formatting

- **FR-1 — One statement emits one line.** A `PICO_LOG_*` statement emits
  exactly one line — timestamp, level label, `file:line`, message, newline —
  when its temporary is destroyed at the end of the statement, whatever the
  message carries: a control byte in message text renders as an escape (`\n`,
  `\r`, `\t`, otherwise `\xHH`), so text from outside cannot end the record
  and begin one naming any file and level it chooses (CWE-117). A backslash
  renders as `\\`, so an escape cannot be told from text that spells one, and
  every byte from 0x80 up passes through, so UTF-8 reads as written. A line
  cut at capacity (BR-2) keeps an escape whole or drops it whole; a cut can
  still fall inside a UTF-8 sequence.

- **FR-2 — A line reaches the sink in exactly one `write()`, followed by one
  `flush()`.** The line arrives whole, newline included. Issuing the header,
  message and newline as separate calls lets a second execution context
  interleave its own line between them, producing a sequence no sink can
  reassemble (`CXX-08.1`).

- **FR-3 — Inserter coverage is by trait, not by a type list.** Inserters exist
  for string literals, `char` arrays, `char*`/`const char*`, anything
  convertible to `std::string_view`, `bool`, `char`, every integral width in
  both signednesses up to 64 bits, and floating point. Any other type renders as
  `<?>` rather than failing to compile, and so does an integer wider than 64
  bits or a `long double` outside `double`'s range, rather than rendering as a
  different value.

  Dispatch is by type trait, so every fundamental integral type is covered
  whichever of them a fixed-width alias names on a given platform — on
  `arm-none-eabi` `std::int32_t` is `long`, on the host `int`.

- **FR-4 — A `char` array is read to its terminator, never to its extent.** The
  length of a `char[N]` operand is the distance to the first NUL within `N`,
  falling back to `N` when there is none. Deduction strips top-level const, so
  this branch takes a mutable scratch buffer as readily as a literal, and reading
  to `N-1` would emit the trailing NULs and, where the buffer was written short
  of its extent, the indeterminate bytes behind them (`CXX-04.5`, `CXX-12.8`).

- **FR-5 — Integers render exactly; floating point renders to six decimal
  places over a restricted domain.**

  Integers are formatted by `std::to_chars`, which is exact and allocates
  nothing.

  Floating point uses `detail::format_float`, not `std::to_chars` and not
  `snprintf`. Both alternatives are excluded, each for its own reason:
  `snprintf("%g")` reaches newlib's allocating formatter (AR-1), and
  `std::to_chars` for a `double` reaches libstdc++'s shortest-round-trip path,
  which calls Ryu's fixed-notation renderer and therefore keeps its lookup
  tables through `--gc-sections`. The gate on the second is the probe's flash
  budget in AR-1; the measurements it was set from are in
  `tests/firmware/measurements.md`.

  The rendering needs IEEE floating-point semantics. Under `-ffast-math` or
  `-ffinite-math-only` the compiler may assume no infinity or NaN exists, so
  `inf` and `nan` cannot be promised, and under `-fassociative-math` or
  `-fno-signed-zeros` (both in `-funsafe-math-optimizations`) the digits are no
  longer correctly rounded and `-0` loses its sign. A translation unit compiled
  with any flag the compiler names is refused at compile time where it logs a
  float or double; text and integers log as usual there. GCC names all four,
  and `-fassociative-math` takes effect only alongside `-fno-signed-zeros`, so
  it is refused with it; clang names only the first two, so a clang build with the last two is
  not refused and its last digit and the sign of zero are unchecked.

  The rendering is:

  | Input | Rendering |
  |---|---|
  | NaN | `nan` |
  | ±infinity | `inf` / `-inf` |
  | ±0 | `0.000000` / `-0.000000` — the sign is preserved |
  | `FLOAT_FIXED_MIN` ≤ \|v\| < `FLOAT_FIXED_MAX` | fixed, `FLOAT_DECIMAL_DIGITS` places |
  | 0 < \|v\| < `FLOAT_FIXED_MIN`, or \|v\| ≥ `FLOAT_FIXED_MAX` | exponent, `d.dddddde±dd` |

  Rounding is half away from zero, applied to the value supplied — which is
  the `double`, not the decimal text a literal was written as. The two differ
  routinely: the `double` nearest `0.1234565` is below that decimal half, so
  its correctly rounded sixth place is 6.

  **Inside the fixed range the digits are the correctly rounded digits of the
  `double` supplied.** Two things establish it. `FLOAT_FIXED_MAX ×
  FLOAT_DECIMAL_SCALE` stays inside 2^53, so the scaled value is an integer a
  `double` represents exactly and can name the digits to print; a
  `static_assert` binds the two (`CXX-11.2`). And the scaling multiplication is
  itself rounded, so the part it discards is recovered exactly and carried into
  the rounding decision — without that step the last place disagrees with the
  correctly rounded digit for a growing fraction of the range above 1e5.

  A buffer shorter than the widest result this can produce is refused, and the
  result is empty. A prefix of a number is indistinguishable from a whole one
  and reads as a value orders of magnitude out (`CXX-04.5`).

  **Outside it the result is approximate.** Normalising into [1, 10) by
  repeated multiplication or division loses precision in the last places, worst
  for subnormals, which need several hundred scaling steps. Exponent notation
  is documented as approximate for that reason; a caller needing exact digits
  at those magnitudes should not be logging them as floating point.

- **FR-6 — A path renders as its final component.** The `file` field is the
  source path with directory components removed, splitting on the later of `/`
  and `\` so a mixed path resolves to its last component.

- **FR-7 — A null `char*`, and `nullptr`, render as `(null)`.** Constructing a
  `std::string_view` from a null pointer is undefined behaviour, and `nullptr`
  converts to one, so both are handled before the generic branch.

- **FR-8 — An unconvertible clock yields no timestamp.** Where the platform
  cannot convert the current time to local time, or the seconds field does not
  fit, the timestamp field is empty rather than a plausible-looking 1900 date.

- **FR-9 — A captured line is a copy, not a view.** `CaptureSink` stores what
  it was handed rather than the caller's buffer. A sink that borrowed would
  dangle the moment the line buffer it was written from went out of scope,
  which for this library is every statement.

- **FR-10 — Framing survives capture.** `lines()` yields one entry per
  `write()`, and `str()` yields the concatenation. The two are not
  interchangeable: a suite asserting that one statement produced one line
  cannot express that against a single string, and a suite matching text
  across a whole burst cannot express it against a vector.

- **FR-11 — Flushes are counted, not merged.** A `flush()` increments a count
  of its own and contributes no line. FR-2 makes one statement exactly one
  write and one flush, so a consumer's suite can only assert that against a
  sink that distinguishes them.

`pico_logging::testing` is a shipped module, so these are a contract other
repositories write tests against rather than an internal detail: a suite
asserting on log output uses this sink rather than writing its own, which is
what makes what a captured line contains one contract instead of one per suite.

---

## 4. Boundary requirements (BR)

- **BR-1 — A message longer than the buffer is cut, never grown and never
  dropped.** Capacity is `LOG_BUFFER_CAPACITY` bytes of message. Operands are
  accumulated until the buffer is full; the remainder of the statement is
  discarded. A log message is diagnostic text, which `CXX-07.2` permits to be
  shortened on two conditions: the cut is marked (BR-2), and no field a reader
  locates the line by is removed (BR-4).

- **BR-2 — Truncation is marked, and the marker always lands whole.** A
  truncated message ends with `...`. Where too little space remains to hold the
  marker, the buffer is rewound far enough to fit it. A marker that is itself
  truncated leaves the line ending in `..`, which reads as complete to a
  consumer matching on the marker (`CXX-07.6`).

- **BR-3 — A message of exactly the capacity is not truncated.** The boundary is
  exclusive: truncation begins one byte beyond capacity.

- **BR-4 — A file name too long for the header is cut from the front, and the
  line number always lands.** The header occupies at most `LOG_HEADER_SIZE`
  bytes. Every header field but the file name is bounded, and `LOG_HEADER_SIZE`
  holds all of them at their widest together with the cut marker — a
  `static_assert` binds the two — so the file name takes what the rest leaves.
  A name longer than that keeps its end, the part that tells two files apart,
  behind a `...` marker; the `:<line>: ` that follows it is never cut
  (`CXX-07.2`, `CXX-07.6`). FR-6 has already removed the directories, so only a
  long final path component reaches this.

- **BR-5 — `RingSink` queues whole lines or none.** A line is enqueued in full,
  or dropped in full and counted. A line longer than one slot is dropped for the
  same reason. Forwarding a prefix would hand the target something that is not a
  line, which FR-2 promises cannot happen.

---

## 5. Concurrency requirements (CR)

- **CR-1 — The current sink and the runtime level are lock-free atomics.** A
  context reading either sees a value that was stored, never a torn one, and
  takes no lock doing so, which is what makes the read safe from an interrupt
  handler that pre-empts a writer. Sequential consistency is used deliberately:
  they are independent scalars with nothing published alongside them
  (`CXX-08.2`).

- **CR-2 — Replacing a sink is not synchronised against a sink in use.**
  `set_sink` publishes the new pointer atomically, so a concurrent statement
  sees the old sink or the new one. It does not wait for a context already
  inside the old sink's `write()`. Sinks are installed during start-up.

- **CR-3 — `StderrSink` is not safe from interrupt context.** It calls
  `std::fwrite`; where newlib is built with FILE locking, a handler
  pre-empting a mainline caller that holds that FILE's lock blocks on a lock only
  the interrupted code can release (`CXX-08.3`). This is stated, not fixed:
  CR-4 removes `std::fwrite` from the handler, and nothing else. CR-5 is what
  remains on that path once it is gone.

- **CR-4 — `RingSink` serialises its producers with a lock the caller
  supplies, and has one consumer.** Installed with `set_sink`, a `RingSink`
  receives every statement in the image — the main loop's as well as any
  interrupt handler's or the second core's — so it always has more than one
  producer. `write()` holds the caller's lock across one line's copy into a
  slot and across nothing else, and allocates nothing. The lock must exclude
  every producer context from every other; one an interrupt handler can
  pre-empt the holder of deadlocks that handler. On the RP2350 that is the
  SDK's `critical_section`, which masks interrupts on the calling core and
  takes a hardware spinlock against the other.

  The consumer forwards queued lines to the target sink without taking the
  lock. The queue count carries the publication in both directions: release on
  enqueue, acquire on dequeue, and the reverse for slot reuse. Two consumers is
  unsupported and undetected.

  **The claim covers `RingSink::write`, not the whole of the producer's
  context.** A statement reaching the sink has already run
  `LogLine::flush_and_finalize`, which formats the timestamp and assembles the
  header first, so `std::chrono::system_clock::now()`, `localtime_r` and
  `strftime` all execute in the producer's context and `sink->write()` is the
  last call on that path rather than the first. CR-5 states what those calls
  require.

- **CR-5 — Logging from interrupt context requires the platform's clock and
  time-zone calls to be safe there.** Every statement reads the clock through
  `std::chrono::system_clock::now()` and formats it with `localtime_r` and
  `strftime`, in the producer's context and before any sink is called.
  `localtime_r` and `strftime` take newlib's TZ lock (`__tz_lock`). Where a
  port supplies a real implementation of that lock — a FreeRTOS build is the
  usual case — or a clock read that blocks, a handler logging while the main
  loop holds the lock meets exactly the hazard CR-3 describes for stdio's FILE
  lock, and `RingSink` does not remove it.

  This is a constraint on the image, not a behaviour of this library, and no
  input to the library can make it fail. Whether a given toolchain and SDK meet
  it is re-derived by inspecting the linked image, with the commands in
  `tests/device/measurements.md`.

---

## 6. Observability and configuration requirements (OR)

- **OR-1 — Filtering happens twice, and the two filters differ in cost.** A
  statement fires when its level passes both the compile-time ceiling
  (`PICO_LOGGING_EFFECTIVE_LEVEL`, default 4) and the runtime level
  (`get_level()`, default `Level::INFO`).

  Above the ceiling the whole statement is discarded by `if constexpr` in the
  macro, so its operands are never evaluated. Below the ceiling but above the
  runtime level, the statement is constructed and each `<<` is a no-op — the
  operands are evaluated. `PICO_LOG_ENABLED_<LEVEL>` is the combined check, for
  guarding work too expensive to evaluate.

- **OR-2 — The compile-time ceiling is per translation unit, and reaches no
  linked entity.** `PICO_LOGGING_EFFECTIVE_LEVEL` is a macro. No `inline`
  function, `inline constexpr` variable or template instantiation may carry its
  value: two translation units compiled at different levels would then hold two
  definitions of one entity, which is an ODR violation the linker resolves by
  discarding one arbitrarily — silently disabling logging that was asked for, or
  enabling logging that was compiled out, depending on link order and
  optimisation level (`CXX-10.3`, `CXX-11.2`).

  Within this library that is a property of its own code, and it holds: the
  value appears only in the five statement macros and the five
  `PICO_LOG_ENABLED_*` macros, every one of which expands at the call site.
  Beyond the library it is a constraint the consumer inherits, because the
  expansion happens in the consumer's translation unit — a `PICO_LOG_*`
  statement inside an `inline` function or a function template in a header puts
  the ceiling into a linked entity, and two of the consumer's own translation
  units at different ceilings then hold two definitions of it. OR-4 makes that
  hard to reach by accident; a per-target override of the ceiling is the case
  where it becomes reachable.

- **OR-3 — `set_level` stores what it is given.** The runtime level is one value
  shared by the whole image and is not capped against any ceiling. A cap would
  read the ceiling of the library's own translation unit, which for an
  installed or fetched package is the default, so `LOGGING_GLOBAL_LEVEL=5`
  would be unreachable for every consumer of one.

- **OR-4 — `PICO_LOGGING_LEVEL` propagates as a `PUBLIC` compile definition.**
  The CMake option sets `PICO_LOGGING_DEFAULT_CEILING` on the owning target so
  it reaches every consumer that links it. A directory-scoped definition reaches
  only this project's own targets (`CXX-10.3`). A target's own
  `LOGGING_GLOBAL_LEVEL` takes precedence over it, as a separate macro rather
  than a redefinition, so a per-target override holds with the option set.

- **OR-5 — `set_sink(nullptr)` restores the built-in default.** `get_sink()`
  never returns null.

- **OR-6 — A dropped line is counted.** `RingSink::dropped()` reports the number
  of lines discarded because the queue was full or the line oversized. The
  counter is written only under the producer lock and read from any context;
  it is relaxed, because it is a statistic ordered against nothing.

- **OR-7 — A statement macro does not capture a trailing `else`.** The macro
  expands to an `if constexpr` whose *discarded* branch is the empty one, so an
  `else` written at the call site binds to the caller's own `if`.

---

## 7. Traceability matrix

Every requirement above maps to the test that establishes it and the criterion
that test applies (`EN-02.5`).

| ID | Test | Pass criterion |
|---|---|---|
| AR-1 | `allocation_test` (`TheLogPathAllocatesNothing`, `TruncationAllocatesNothing`, `RingSinkQueuesAndDrainsWithoutAllocating`); the `symbols` gate tables `allocation (log probe)` and `heap (log probe objects)` in `pico.toml`; `lint` over `src/` and CTest `pico_logging_printf_rule_refuses_snprintf` and `pico_logging_debug_string_has_no_throw_path` | The counter reads zero across a burst covering every inserter branch and the escaping of a control byte and a backslash; the cross-compiled image defines no `_dtoa_r`/`_Balloc` and the probe's objects reach no allocator; no printf-family call appears in `src/`; the -Og probe objects reference no `__throw_out_of_range_fmt` |
| AR-1 (footprint) | the `footprint` gate table `log probe` in `pico.toml` | The probe's static RAM (allocated sections excluding read-only load images at their LMA, heap and stack reservations) and flash (all `LOAD` sections) each stay inside their budget, so a formatting choice cannot add a step change to either while the allocation gates stay green |
| AR-1 (gate integrity) | the `[[symbols]]` controls `allocation refuses its fixture` and `heap refuses its fixture` in `pico.toml` (`expect = "refused"`, over `tests/firmware/allocating_fixture.cpp` and `tests/firmware/heap_call_fixture.cpp`); `tests/varargs/printf_rule_control.py` (over `tests/varargs/varargs_call.cpp.in`); `allocation_test` (`TheCounterObservesAnAllocation`) | Each gate **fails**, naming its finding, on an input it must reject, and the host counter does observe an allocation — so a pass from any of them means something |
| AR-2 | `tests/device/ar2_heap_watermark.cpp`, run on device; the image inspection recorded with it | Counting calls to `_malloc_r` across a burst through the default sink gives zero, while a deliberate allocation in the same run is observed — so the zero is a measurement rather than a dead counter; and the image links `__swsetup_r` and `__smakebuf_r`, and `__smakebuf_r` calls `_malloc_r`, re-derived with the commands the record carries. Procedure, and the image inspection's 0.1.0 result, in `tests/device/measurements.md`; the device run on 0.1.0 is pending |
| AR-3 | The compiler, at `src/logging/sinks.cpp` (`constinit const LogState LOG_STATE_STORAGE{}`); `logging_test` sink and level tests | Module state is constant-initialised, so it is reachable before any constructor has run and carries no guard variable. If that stopped holding, every build fails at that line, host and cross alike |
| AR-4 | `ring_sink_test` (`AFullRingDropsWholeLinesAndCountsThem`, `IndicesWrapAndDrainedSlotsAreReused`) | Capacity is the template parameter and does not grow under load. That every buffer in the module is a fixed-extent array is review-only: no input can make a buffer grow, so what establishes it is reading the declarations |
| AR-5 | `tests/device/cr4_ring_sink_costs.cpp`, run on device; the record in `tests/device/measurements.md` | The deepest of the measured statements — the first in the image, fixed and exponent floats, strings, and a message cut at capacity — reaches no more than 1,792 bytes below its caller in a `Release`, a `Debug` and a `MinSizeRel` build on the RP2350, through a `RingSink` and through the default `StderrSink` with the USB interrupt live, with stdio over USB alone and over USB and UART, while a 512-byte control frame in the same run is observed at 512 or more, so the depth is a measurement rather than an unpainted stack; the run on 0.1.0 is pending. That a consumer's sink does not log is review-only: the sink is outside this tree |
| AR-6 | the consumer case `pico-w-refused` in `house.toml`, built by the `consumer` gate over `tests/consumer-board/` | A consumer adding the library on `pico_w` fails at configure, saying `PICO_LOGGING needs PICO_PLATFORM rp2350` |
| FR-1 | `logging_test` (`LogLineWritesWhenEnabled`, `HeaderStartsWithFormattedTimestamp`, `LabelAndColorMatchLevels`, `ControlCharactersInTextAreEscaped`, `TheCutNeverSplitsAnEscape`, `TheCutNeverSplitsAdjacentEscapes`) | One statement produces one newline-terminated line carrying a complete timestamp, the level label, and `<file>:<line>: ` immediately before the message; a newline, carriage return, tab, escape or delete byte in text streamed as a `string_view`, a `const char*`, a literal or a `char` renders as its escape, a backslash doubles, a UTF-8 sequence passes through unchanged, and the line's only newline is its last byte; a line cut at capacity keeps an escape whole or drops it whole, at every position the cut can reach |
| FR-2 | `logging_test` (`EachLineIsExactlyOneWriteAndOneFlush`) | Exactly one `write()` and one `flush()` per statement, the write carrying the whole line |
| FR-3 | `logging_test` (`FormatsAllIntegerWidthsAndScalars`, `FormatsIntegerExtrema`, `EveryFundamentalIntegralTypeRendersItsValue`, `ATypeWithNoInserterRendersAsAPlaceholder`); `wide_value_test`, built with GNU extensions | Every signed and unsigned integer width from 8 to 64 bits renders its value; `string_view`, `std::string`, `float` and `double` render; `<?>` never appears for a supported type and does appear for a type with no inserter. `char8_t`, `char16_t`, `char32_t` and `wchar_t`, being integral, render their code value; an `unsigned __int128`, and a `long double` too large or too small for `double`, render `<?>`, while a `long double` infinity renders `inf` |
| FR-4 | `logging_test` (`CharArrayInserterStopsAtTheTerminator`, `CharArrayInserterHandlesTheEdgeShapes`) | A partly filled array renders its string with no NUL bytes reaching the sink; an unterminated array renders whole |
| FR-5 | `float_format_test` (every case, `MatchesACorrectlyRoundedReferenceAcrossTheFixedRange` and `MatchesTheReferenceBesideEveryRoundingHalf` in particular; the four `pico_logging_refuses_*_float` build-failure tests); `logging_test` (`FormatsAllIntegerWidthsAndScalars`, `EachLineIsExactlyOneWriteAndOneFlush`) | Integers render exactly; across the fixed range every rendering matches a correctly rounded reference, sampled per decade, and so does a value at and beside a sixth-place rounding half, sampled per decade, where a dropped or mis-signed residual changes the last digit; an exact half rounds away from zero; NaN, infinities and negative zero render as themselves; the notation switches at both thresholds; a rounding carry renormalises the exponent; no result exceeds `NUMERIC_FORMAT_SIZE`; a buffer below the documented worst case yields an empty view and is not written to; under `-ffast-math` the `pico_logging_fast_math_probe` object, logging text and an integer, builds, and `pico_logging_refuses_fast_math_float`, logging a double, is refused with the IEEE-semantics diagnostic, and so is `pico_logging_refuses_finite_math_float` under `-ffinite-math-only`, and on GCC `pico_logging_refuses_unsafe_math_float` under `-funsafe-math-optimizations` and `pico_logging_refuses_unsigned_zero_float` under `-fno-signed-zeros` |
| FR-6 | `logging_test` (`FilenameOnlyStripsDirectorySeparators`) | Each separator style, a mixed path, and a bare separator resolve to the final component |
| FR-7 | `allocation_test` (`TheLogPathAllocatesNothing`, which inserts a null `const char*`); `logging_test` (`ANullCharPointerRendersAsNull`, `NullptrRendersAsNull`) | A null `char*` and `nullptr` render as `(null)`, fault nothing, and a null `char*` allocates nothing |
| FR-8 | `logging_test` (`HeaderStartsWithFormattedTimestamp`, `ATooSmallBufferLeavesTheTimestampEmpty`) | A complete `YYYY-MM-DD HH:MM:SS.mmm`, or an empty field — never a partial or 1900 date. The too-small-buffer arm is tested; the unconvertible-clock arm is review-only, because `system_clock` is nanosecond-based and every instant it represents is inside `localtime_r`'s range |
| FR-9 | `capture_sink_test` (`CopiesRatherThanBorrowingTheView`) | A line whose source buffer is overwritten after the call reads back as it was written |
| FR-10 | `capture_sink_test` (`LinesDistinguishesFramingThatStrCannot`, `RecordsEachLineWholeAndInOrder`) | `lines()` is one entry per `write()`, in order; `str()` is their concatenation |
| FR-11 | `capture_sink_test` (`CountsFlushesSeparately`, `ClearDiscardsEverythingIncludingFlushes`, `WorksThroughTheSinkInterface`) | A flush increments the flush count and adds no line; `clear()` resets both |
| BR-1 | `logging_test` (`TruncatesOverlongMessageWithSingleMarker`) | The line stays inside header + buffer; operands after truncation are discarded |
| BR-2 | `logging_test` (`TheTruncationMarkerAlwaysLandsWhole`) | For every remaining-byte count from 0 to 4, the line ends in `...` and not `..` |
| BR-3 | `logging_test` (`TruncatesOnlyBeyondTheExactCapacity`) | Capacity and capacity−1 do not truncate; capacity+1 does |
| BR-4 | `logging_test` (`AnOverlongFileNameIsCutFromTheFrontAndTheLineSurvives`); the compiler, at `LOG_HEADER_FIXED_MAX`'s `static_assert` | A file name longer than the header leaves renders as `...` followed by its end, then `:<line>: ` and the message, and the line stays inside the header reservation. If the fixed fields could no longer fit beside the marker, every build fails at the assertion |
| BR-5 | `ring_sink_test` (`AFullRingDropsWholeLinesAndCountsThem`, `AnOversizedLineIsDroppedRatherThanTruncated`, `LinesRoundTripUnchanged`) | A dropped line contributes no bytes; a kept line round-trips byte for byte |
| CR-1 | The compiler, at `src/logging/sinks.cpp` (`is_always_lock_free` for both atomics); `logging_test` (`SetLevelStoresWhatItIsGiven`, `CustomSinkReceivesOutputAndDefaultRestored`) | Both are lock-free atomics on every target the library builds for, or the build fails; stored values are read back exactly |
| CR-2 | `logging_test` (`CustomSinkReceivesOutputAndDefaultRestored`) for the publication; review of `set_sink` for the limitation | A statement after `set_sink` reaches the new sink. That `set_sink` does not wait for a context already inside the old sink's `write()` is a limitation rather than a behaviour: no input can make the library fail to honour it, so it is established by the documentation on `set_sink` and `get_sink` |
| CR-3 | Review of `StderrSink`'s class note in `sinks.hpp` | The note states the constraint and names `RingSink` as the supported route from interrupt context, which CR-4 tests. Review-only: reproducing the hazard needs a newlib built with FILE locking, an interrupt and a deliberate deadlock, a test that hangs on success |
| CR-4 | `ring_sink_test` (`EveryWriteTakesTheProducerLockOnceAndDrainNever`, `SurvivesConcurrentProducersAndAConsumer`, `TheStreamCheckRejectsEachFault`), and the same suite as `RingSinkTest.UnderThreadSanitizer` | Every `write()` takes the lock exactly once, on the dropping paths too, and `drain()` never does. Under three producer threads and a consumer, every forwarded line is one that was offered, byte for byte, each producer's lines arrive in order with none repeated, and forwarded + dropped equals offered; the check applying that is shown to reject a torn, repeated, reordered or uncounted line; ThreadSanitizer reports no race |
| CR-5 | `tests/device/measurements.md` — "CR-5: does the timestamp path take a lock?" | For each toolchain and SDK recorded there, `__tz_lock` is `bx lr` or branches to a `__retarget_lock_acquire` that is `bx lr`, and the SDK's clock read takes no lock. A port that retargets newlib's locks makes those bodies real and is outside what CR-5 permits; re-derived with the commands the record carries |
| OR-1 | `logging_test` (`CompileTimeCeilingDiscardsOperandsAndRuntimeFilterDoesNot`, `LogEnabledCombinesBothFilters`) | Above the ceiling the operand is not evaluated; below it but above the runtime level it is, and nothing is emitted |
| OR-2 | `logging_test` (`IsEnabledReflectsTheRuntimeLevelAlone`); `level_odr_test` (`EachUnitLogsAtTheLevelItWasCompiledWith`); ctest `pico_logging_level_ceiling_is_macro_only`, `pico_logging_level_ceiling_check_refuses` | `is_enabled` answers the runtime question only; two units at different ceilings each behave as compiled; outside comments, every line of every file under `src/` naming the ceiling — or a macro whose `#define` names it or sits in a conditional on it — is a preprocessor directive or a continued `#define`, and a conditional on the ceiling contains only preprocessor lines. The check refuses an `inline constexpr` variable, a function reading an enable macro, a function logging through a statement macro, a definition selected by `#if` or by `#ifdef` on the ceiling, a macro so selected and then read, an alias, and a use in a header of another extension — forms the behavioural case cannot see, because the optimiser folds them |
| OR-3 | `logging_test` (`SetLevelStoresWhatItIsGiven`); `level_odr_test` (`TheRuntimeLevelIsSharedAndUncapped`) | `set_level(TRACE)` is read back as `TRACE` regardless of the library's own ceiling |
| OR-4 | the consumer case `level-five` in `house.toml` (`tests/consumer-level/`) and `tests/consumer/consumer_override.cxx`, both built by the `consumer` gate | A consumer of a package installed at `PICO_LOGGING_LEVEL=5` compiles with `PICO_LOGGING_DEFAULT_CEILING` equal to 5, stated by a `static_assert` in its own translation unit, and a consumer target defining its own `LOGGING_GLOBAL_LEVEL` under `-Werror` compiles at that ceiling |
| OR-5 | `logging_test` (`CustomSinkReceivesOutputAndDefaultRestored`) | After `set_sink(nullptr)`, `get_sink()` is neither null nor the removed sink |
| OR-6 | `ring_sink_test` (`AFullRingDropsWholeLinesAndCountsThem`, `AnOversizedLineIsDroppedRatherThanTruncated`, `SurvivesConcurrentProducersAndAConsumer`) | `dropped()` equals the number of lines refused, alone and under concurrent producers |
| OR-7 | `logging_test` (`AFilteredStatementDoesNotCaptureATrailingElse`) | A trailing `else` runs for the caller's condition, not the macro's |
