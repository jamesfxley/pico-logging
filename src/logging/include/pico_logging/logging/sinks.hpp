// SPDX-License-Identifier: MIT
#pragma once

#include <string_view>

namespace pico_logging {

/// Destination for formatted log lines. A line arrives whole, newline included,
/// in one write() followed by one flush() (FR-2), so an implementation may
/// treat each call as one record.
///
/// The installer (set_sink()) keeps ownership and must keep the sink alive
/// while it is installed. A sink runs in whichever context logs, so one reached
/// from an interrupt handler must itself be safe there; see RingSink.
struct Sink {
    Sink() = default;
    // Neither copyable nor movable, so a sink cannot be sliced (C++ Core
    // Guidelines C.67).
    Sink(const Sink&) = delete;
    Sink& operator=(const Sink&) = delete;
    Sink(Sink&&) = delete;
    Sink& operator=(Sink&&) = delete;
    virtual ~Sink() = default;

    /// Writes one complete log line, including its trailing newline. `text`
    /// points into the LogLine temporary that makes this call, so copy anything
    /// that must outlive it, as RingSink does.
    ///
    /// An implementation must not log: this call is made inside a log
    /// statement, so a PICO_LOG_* here recurses without bound (AR-5). Report a
    /// sink's own failure another way, as RingSink's drop count does.
    virtual void write(std::string_view text) = 0;

    /// Called once per completed line. The default does nothing.
    virtual void flush() {}
};

/// Default sink: writes to stderr, which the Pico SDK routes to the configured
/// USB CDC or UART stdio. Not safe from interrupt context: std::fwrite can
/// block on a FILE lock that the interrupted code holds (CR-3). To log from an
/// interrupt handler, put a RingSink in front of it.
class StderrSink : public Sink {
   public:
    /// Writes the line to stderr with std::fwrite.
    void write(std::string_view text) override;
    /// Flushes stderr, so a line survives a crash that follows it.
    void flush() override;
};

}  // namespace pico_logging
