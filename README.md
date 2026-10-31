# pico-logging

[![CI](https://github.com/jamesfxley/pico-logging/actions/workflows/ci.yml/badge.svg)](https://github.com/jamesfxley/pico-logging/actions/workflows/ci.yml)

`pico-logging` is a small C++20 logging library for Raspberry Pi Pico 2
(RP2350) projects and their host-side tests. It provides level-filtered `PICO_LOG_*`
stream macros that emit one line per statement — timestamp, colored level
label, `file:line` source location, then the message — through a pluggable
`Sink`. The log path is **allocation-free**: each line is staged in a
fixed-size stack buffer and truncated with a `...` marker rather than growing.
That is gated rather than asserted — a host allocation counter over every
inserter, and a cross-compiled image checked for newlib's allocating formatter,
each with a negative control proving the gate can fail.

The [architecture](docs/architecture.md) explains formatting and sinks.

Behaviour is specified in [docs/technical-specification.md](docs/technical-specification.md),
which is normative; each guarantee carries a requirement ID and a test.

All public symbols live in `namespace pico_logging` with headers under the
`pico_logging/` prefix. A consumer links `pico_logging::core`, the umbrella over
the one compiled module, `pico_logging::logging`; a consumer's test suite adds
`pico_logging::testing`, the host-only capture sink.

## Quick start

```cmake
include(FetchContent)
FetchContent_Declare(pico_logging
    GIT_REPOSITORY https://github.com/jamesfxley/pico-logging.git
    GIT_TAG v0.1.0)  # pin the release you build against
FetchContent_MakeAvailable(pico_logging)

target_link_libraries(app PRIVATE pico_logging::core)
```

<!-- example: readme_quick_start -->
```cpp
#include <pico_logging/logging/logging.hpp>

int main() {
    PICO_LOG_INFO << "Pico boot complete, attempt " << 3;
}
```

produces (colors omitted):

```
2026-07-14 08:31:07.412 [INFO] main.cpp:4: Pico boot complete, attempt 3
```

The five macros are `PICO_LOG_ERROR`, `PICO_LOG_WARN`, `PICO_LOG_INFO`,
`PICO_LOG_DEBUG`, and `PICO_LOG_TRACE`. Inserters are provided for string
literals, `char` arrays, `const char*`, anything convertible to
`std::string_view`, `bool`, `char`, every integer width, and floating point;
anything else renders as the placeholder `<?>`.

A `PICO_LOG_*` statement is a statement, not an expression — it expands to an
`if constexpr`, which is what lets a filtered-out statement disappear entirely.

## Layout

The source layout is in [docs/orientation.md](docs/orientation.md). The one
dependency is [pico-util](https://github.com/jamesfxley/pico-util), the family's
base layer, whose `RingBuffer` holds `RingSink`'s slots.

## Building & tests

Every tool the gates need, the Pico SDK included, is in the pico family
development image that `.devcontainer/devcontainer.json` builds from `infra/`;
open the repository in its dev container to use that image. The `infra/` git
submodule ([pico-tooling](https://github.com/jamesfxley/pico-tooling)) carries
the gates, the CMake helpers and the pinned guides; clone with
`git clone --recurse-submodules` or run
`git submodule update --init --recursive` after a plain clone. Flash the device
test with the family device CLI described in
[tests/device/README.md](tests/device/README.md).

```bash
python3 dev.py check    # every gate; a change is done when this is green
python3 dev.py build    # clean build and test of every enabled tree
```

Tests are configured by default only when pico-logging is the top-level project
and not cross-compiling; `PICO_LOGGING_ENABLE_TESTS` overrides the default.

## Level filtering

The [usage guide](docs/usage-guide.md) shows ceiling and runtime-level setup.
Use `PICO_LOG_ENABLED_<LEVEL>` to guard expensive operands:

<!-- example: readme_debug_guard -->
```cpp
if (PICO_LOG_ENABLED_DBG) {
    PICO_LOG_DEBUG << expensive_summary();
}
```

## Sinks

Output goes through the `Sink` interface (`sinks.hpp`); the default is
`StderrSink`, which works on both host and firmware (the Pico SDK routes
stderr to USB CDC/UART stdio). To redirect output, implement `Sink::write()` and
install it:

<!-- example: readme_uart_sink -->
```cpp
struct UartSink : pico_logging::Sink {
    void write(std::string_view text) override { uart_write(text); }  // your UART transmit
};

void install_uart_sink() {
    static UartSink uart_sink;            // must outlive all logging
    pico_logging::set_sink(&uart_sink);   // nullptr restores the default
}
```

### Logging from an interrupt handler

Use `RingSink` for interrupt callers; the
[architecture](docs/architecture.md) explains the lock and stdio constraints:

<!-- example: firmware/readme_ring_sink -->
```cpp
// The producer lock: masks interrupts on this core and takes a hardware
// spinlock against the other, so handlers and core 1 can all log.
struct CriticalSection {
    critical_section_t section{};
    CriticalSection() { critical_section_init(&section); }
    void lock() { critical_section_enter_blocking(&section); }
    void unlock() { critical_section_exit(&section); }
};

pico_logging::RingSink<8, CriticalSection>& log_ring() {
    static pico_logging::StderrSink uart;
    static CriticalSection ring_lock;
    static pico_logging::RingSink<8, CriticalSection> ring(uart, ring_lock);
    return ring;
}

void start_logging() { pico_logging::set_sink(&log_ring()); }   // during start-up
void service_logging() { log_ring().drain(); }  // in the main loop; forwards to `uart`
```

The lock is held across one line's copy and nothing else. `drain()` is called
from one context. A line is queued whole or dropped whole, and `dropped()`
counts the difference. The [usage guide](docs/usage-guide.md) has the rest.

Timestamps come from `std::chrono::system_clock`. On firmware that counts
from boot unless the wall clock has been synchronized — the companion
[`pico-time`](https://github.com/jamesfxley/pico-time) library provides NTP
sync and the newlib shims that make `system_clock` reflect it.

Build infrastructure is shared through the `infra/` submodule
([pico-tooling](https://github.com/jamesfxley/pico-tooling)); the family
bootstrap is `cmake/pico_family_bootstrap.cmake`.

## Documentation

- [Documentation index](docs/README.md) — what each document is for
- [Orientation](docs/orientation.md) — how the repository is laid out, and the
  traps it has already paid for
- [Contributing](CONTRIBUTING.md) — how a change is made and when it is done
- [Technical specification](docs/technical-specification.md) — normative;
  requirement IDs `AR/FR/BR/CR/OR`, their pass criteria and the traceability
  matrix
- [Architecture](docs/architecture.md) — a statement's life, the two filters,
  the float formatter, the sinks, and what this library deliberately is not
- [Usage guide](docs/usage-guide.md) — bringing it up in firmware, choosing a
  filter, the per-TU ceiling, logging from an interrupt handler
- [Standing deviations](docs/deviations.md) — scope-wide rule suppressions and
  their review dates
- [Releases](https://github.com/jamesfxley/pico-logging/releases) — what changed
  per release, and what to change in your code when an API broke

Run records live beside the rig that produced them:
[tests/device/measurements.md](tests/device/measurements.md) for the AR-2
device run, [tests/firmware/measurements.md](tests/firmware/measurements.md)
for the cross-compiled probe's footprint.

## Releases

Versioning is semver under `EN-06.1`: a break lands at a minor bump while the
major version is 0, at a major bump from 1.0. The notes on
each [release](https://github.com/jamesfxley/pico-logging/releases) record what
changed and, for a break, what to change in your own code (`EN-06.3`).

## AI-assisted development

This project is developed with AI assistance. Design decisions, review and
acceptance are human; an AI agent writes and refactors code under direction and
against a written engineering standard. Every change passes the repository's own
gates — build, tests, formatter, linter, deviation counts — before it lands.

This section is disclosure, not a warranty. The terms in [LICENSE](LICENSE)
apply unchanged, including its disclaimer of warranties.

## Development configuration

`house.toml` refers to `infra/family.toml`; the pinned family supplies gates,
trees, CI preparation and synced file selections. Product dependency pins live
in `CMakeLists.txt`. `AGENTS.md` routes to `infra/agents/AGENTS.md`.
House renders root tool files, the workflow and `.devcontainer/devcontainer.json`;
`python3 -m infra.pico_tooling sync` writes the bootstrap. Tests own their
registered `.clang-tidy` overlay. Infrastructure freshness reports adoption as a
warning and passes; unreadable release tags are unavailable evidence.
