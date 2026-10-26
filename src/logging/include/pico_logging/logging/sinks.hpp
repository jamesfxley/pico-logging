// SPDX-License-Identifier: MIT
#pragma once

#include <string_view>

namespace pico_logging {

/// Destination for formatted log output. Implementations can write to
/// stderr, UART, files, an in-memory capture for tests, etc.
///
/// **Exactly one write() per line, followed by one flush().** The line arrives
/// whole, newline included, so an implementation may treat each call as one
/// record — which is what lets a queueing sink forward lines without having to
/// reassemble them, and two contexts logging concurrently cannot interleave
/// the halves of their lines (FR-2, CXX-08.1).
///
/// Install with set_sink() (logging.hpp); the installer keeps ownership and
/// must keep the sink alive while it is installed. A sink is called from
/// whichever context logs, so one that will be reached from an interrupt
/// handler must itself be safe there — see RingSink (ring_sink.hpp).
struct Sink {
    Sink() = default;
    // Polymorphic base: non-copyable/movable so slicing can't happen and a
    // sink is only ever referred to by pointer (C++ Core Guidelines C.67).
    Sink(const Sink&) = delete;
    Sink& operator=(const Sink&) = delete;
    Sink(Sink&&) = delete;
    Sink& operator=(Sink&&) = delete;
    virtual ~Sink() = default;

    /// Write one complete log line, including its trailing newline. A line
    /// arrives in exactly one call (FR-2), so an implementation may treat one
    /// call as one record — two contexts logging concurrently cannot interleave
    /// halves of a line into a sequence no sink could reassemble.
    ///
    /// `text` is borrowed for the duration of the call: copy anything that must
    /// outlive it. One call being one record makes a queueing sink attractive,
    /// and that is exactly where the mistake lands — the view points into the
    /// LogLine temporary the logging statement built, and it is that
    /// temporary's destructor making this call. Nothing of `text` survives the
    /// return. RingSink copies into a slot for this reason (ring_sink.hpp).
    ///
    /// An implementation must not log. This call is made from inside a log
    /// statement, so a PICO_LOG_* here re-enters the sink and recurses without
    /// bound, and AR-5's stack bound rests on the path not recursing. Report a
    /// sink's own failure some other way: a counter the application reads, as
    /// RingSink's drop count is.
    virtual void write(std::string_view text) = 0;

    /// Called once per completed log line; default is a no-op for sinks
    /// that write through immediately.
    virtual void flush() {}
};

/// Default sink: writes to stderr, which works on both host and firmware
/// (the Pico SDK routes stderr to the configured USB CDC/UART stdio).
///
/// **Not safe from interrupt context (CR-3).** It calls std::fwrite, and where
/// newlib is built with FILE locking — the FreeRTOS-on-SDK configuration — a
/// handler that pre-empts a mainline caller holding that FILE's lock blocks on
/// a lock only the interrupted code can release (CXX-08.3). To log from an
/// interrupt handler, install a RingSink (ring_sink.hpp) in front of this one
/// and drain it from the main loop.
class StderrSink : public Sink {
   public:
    /// Write the line to stderr with std::fwrite. See the class note: not safe
    /// from interrupt context.
    void write(std::string_view text) override;
    /// Flush stderr, so a line survives a crash that follows it.
    void flush() override;
};

}  // namespace pico_logging
