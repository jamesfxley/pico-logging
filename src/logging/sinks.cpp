// SPDX-License-Identifier: MIT
#include "pico_logging/logging/sinks.hpp"

#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <string_view>
#include <system_error>

#include "pico_logging/logging/logging.hpp"
#include "pico_logging/pico_logging.hpp"

// The advertised version and project(pico_logging VERSION ...) are two records
// of one fact. Bumping either alone stops the build here.
static_assert(pico_logging::VERSION_MAJOR == PICO_LOGGING_CMAKE_VERSION_MAJOR);
static_assert(pico_logging::VERSION_MINOR == PICO_LOGGING_CMAKE_VERSION_MINOR);
static_assert(pico_logging::VERSION_PATCH == PICO_LOGGING_CMAKE_VERSION_PATCH);

namespace pico_logging {

// fwrite/fflush rather than std::cerr: iostreams drag the whole locale +
// stream machinery (flash footprint and static-init cost) into every
// firmware image that links logging. The Pico SDK routes stderr to the
// configured USB CDC / UART stdio.
void StderrSink::write(std::string_view text) {
    // A sink cannot report its own failure -- the only channel for saying so
    // is the sink. A short write loses log output and nothing else, so the
    // count is deliberately dropped rather than escalated, and the cast says
    // so in the language rather than by silencing the check.
    static_cast<void>(std::fwrite(text.data(), 1, text.size(), stderr));
}

// Same reasoning as write(): a failed flush has no reportable channel.
void StderrSink::flush() { static_cast<void>(std::fflush(stderr)); }

namespace {
// Module state. Atomics because firmware logs from the lwIP background/IRQ
// context while set_sink()/set_level() may run on the main loop; seq_cst is
// left as the default deliberately (CXX-08.2), since these are two independent
// scalars with nothing to publish alongside them and no hot path to pay for it.
//
// AR-3: constinit at namespace scope, not a function-local static. A
// function-local static is constructed on first use behind a guard, so a log
// from interrupt context could observe a half-constructed StderrSink or
// re-enter __cxa_guard_acquire on a guard the mainline holds (CXX-08.3).
// Constant initialisation happens before any code runs, so there is no guard
// and no ordering to lose.
// The object itself never changes -- it is const, and the two values that do
// change are `mutable` atomics. That is what the state actually is, and it
// keeps a mutable global out of the module (cppcoreguidelines).
struct LogState {
    // default_sink is mutable for the same reason as the atomics: writing to a
    // sink mutates it, and set_sink hands out a pointer to this one.
    mutable StderrSink default_sink;
    mutable std::atomic<Sink*> current_sink;
    mutable std::atomic<Level> runtime_level;

    constexpr LogState() noexcept
        : current_sink(&default_sink), runtime_level(Level::INFO) {}
};

constinit const LogState LOG_STATE_STORAGE{};

// CR-1 is a property of the types, so the compiler establishes it: a lock-free
// atomic is never torn and never takes a lock, so it is also safe to read from
// an interrupt handler that pre-empts a writer.
static_assert(std::atomic<Sink*>::is_always_lock_free,
              "the current sink must be a lock-free atomic (CR-1)");
static_assert(std::atomic<Level>::is_always_lock_free,
              "the runtime level must be a lock-free atomic (CR-1)");

const LogState& log_state() noexcept { return LOG_STATE_STORAGE; }
}  // namespace

Sink* get_sink() noexcept { return log_state().current_sink.load(); }

void set_sink(Sink* sink) noexcept {
    const LogState& state = log_state();
    state.current_sink.store(sink != nullptr ? sink : &state.default_sink);
}

Level get_level() noexcept { return log_state().runtime_level.load(); }

// Stores what it is given. The runtime level is one shared value; the
// compile-time ceiling is per translation unit and is applied by the PICO_LOG_*
// macro at the statement. A cap here would read this file's ceiling, which for
// an installed or fetched package is the default, so a consumer built at
// LOGGING_GLOBAL_LEVEL=5 could not reach Level::TRACE.
void set_level(Level level) noexcept { log_state().runtime_level.store(level); }

// std::to_chars rather than snprintf for integers (CXX-11.1, AR-1). It
// allocates nothing, takes no varargs and returns a checkable result, so
// the rules a varargs formatter would trip do not apply here and nothing
// needs suppressing (CXX-00.1). The integer overloads are small: none
// of the Ryu machinery that makes the floating-point ones expensive is
// reachable from them.
template <typename T>
void LogLine::write_number(T value) {
    std::array<char, NUMERIC_FORMAT_SIZE> temp{};
    const std::to_chars_result result =
        std::to_chars(temp.data(), temp.data() + temp.size(), value);
    if (result.ec != std::errc{}) {
        // NUMERIC_FORMAT_SIZE is sized for the widest of these types, so
        // this is unreachable rather than a capacity policy. Render a
        // marker instead of silently emitting nothing (CXX-07.6).
        write_str("<num>");
        return;
    }
    write_str(std::string_view(
        temp.data(), static_cast<std::size_t>(result.ptr - temp.data())));
}

// FR-3: operator<< normalizes supported integers to these two widths.
template void LogLine::write_number(std::int64_t value);
template void LogLine::write_number(std::uint64_t value);

}  // namespace pico_logging
