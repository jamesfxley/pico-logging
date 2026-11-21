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

// fwrite and fflush and not std::cerr: iostreams pull the locale and stream
// machinery into every firmware image that links logging.
void StderrSink::write(std::string_view text) {
    // The only failure channel is the sink itself, and a short write loses
    // only log output, so the count is discarded. The cast makes that explicit
    // and needs no suppression.
    static_cast<void>(std::fwrite(text.data(), 1, text.size(), stderr));
}

// As in write(), a failed flush has no channel to report it.
void StderrSink::flush() { static_cast<void>(std::fflush(stderr)); }

namespace {
// Module state. The atomics let firmware log from the lwIP background or IRQ
// context while set_sink() or set_level() runs on the main loop. They keep the
// default sequentially consistent order, which gives the sink pointer its
// release store and acquire load (CR-1).
//
// constinit at namespace scope and not a function-local static, whose guard a
// first log from interrupt context can trip (AR-3).
//
// The object is const and what changes is mutable members, which keeps a
// mutable global out of the module.
struct LogState {
    // Mutable because writing to a sink mutates it, and set_sink hands out a
    // pointer to this one.
    mutable StderrSink default_sink;
    mutable std::atomic<Sink*> current_sink;
    mutable std::atomic<Level> runtime_level;

    constexpr LogState() noexcept
        : current_sink(&default_sink), runtime_level(Level::INFO) {}
};

constinit const LogState LOG_STATE_STORAGE{};

// Lock-free, so a read from an interrupt handler that pre-empts a writer is
// safe (CR-1).
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

// Stores what it is given and applies no ceiling (OR-3).
void set_level(Level level) noexcept { log_state().runtime_level.store(level); }

// std::to_chars and not snprintf: it allocates nothing (AR-1), and the Ryu
// machinery behind its floating-point overloads is not reachable from these.
template <typename T>
void LogLine::write_number(T value) {
    std::array<char, NUMERIC_FORMAT_SIZE> temp{};
    const std::to_chars_result result =
        std::to_chars(temp.data(), temp.data() + temp.size(), value);
    if (result.ec != std::errc{}) {
        // NUMERIC_FORMAT_SIZE fits the widest of these types, so this branch
        // is unreachable; the marker keeps a failure from passing silently.
        write_str("<num>");
        return;
    }
    write_str(std::string_view(
        temp.data(), static_cast<std::size_t>(result.ptr - temp.data())));
}

// operator<< normalises supported integers to these two widths (FR-3).
template void LogLine::write_number(std::int64_t value);
template void LogLine::write_number(std::uint64_t value);

}  // namespace pico_logging
