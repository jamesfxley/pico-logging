// SPDX-License-Identifier: MIT
#pragma once

/// \file capture_sink.hpp
/// A Sink that records what was logged, for tests to assert on.
///
/// This library's `Sink` is the most-implemented interface in the pico-*
/// family. This is the one double for it: a suite asserting on log output uses
/// this rather than writing its own, so what a captured line contains is one
/// contract rather than one per suite.
///
/// That contract is FR-9 (a captured line is a copy, not a view), FR-10
/// (`lines()` keeps the framing `str()` cannot express) and FR-11 (a flush is
/// counted and contributes no line).
///
/// It ships rather than living under `tests/`: the consumers are downstream
/// test trees, and a hand-written double drifts from the seam it stands in for.
///
/// Test support, so it uses the heap freely — the zero-post-init-heap rule
/// governs the library on a device, not the fakes. **That is why it does not
/// replace every double in this repository:** `allocation_test.cpp` needs a
/// sink that provably allocates nothing, and the firmware probe needs one that
/// compiles without `<string>`. Those two are not duplication; they are
/// different requirements.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "pico_logging/logging/sinks.hpp"

namespace pico_logging::testing {

/// Records every line written to it, whole and in order.
///
/// Copies each line on arrival, which is what `Sink::write`'s contract requires
/// — the view it is handed points into the caller's `LogLine` temporary and
/// does not survive the call.
class CaptureSink final : public Sink {
   public:
    /// Record one line. Never truncates and never drops.
    ///
    /// \param text the line, including its trailing newline.
    void write(std::string_view text) override {
        lines_.emplace_back(text);
        all_.append(text);
    }

    /// Count a flush. The sink writes through, so there is nothing to do.
    void flush() override { ++flushes_; }

    /// \return every line written, concatenated — what a consumer would have
    ///         seen on the wire. Use this to assert on content.
    [[nodiscard]] const std::string& str() const noexcept { return all_; }

    /// \return one entry per `write()` call. Use this to assert on framing:
    ///         the contract is one call per line, so a test that cares whether
    ///         two records were interleaved reads this rather than str().
    [[nodiscard]] const std::vector<std::string>& lines() const noexcept {
        return lines_;
    }

    /// \return the most recent line, or an empty view if nothing was written.
    [[nodiscard]] std::string_view last() const noexcept {
        return lines_.empty() ? std::string_view{}
                              : std::string_view{lines_.back()};
    }

    /// \return how many times `write()` was called, which is how many whole
    ///         lines arrived.
    [[nodiscard]] std::size_t writes() const noexcept { return lines_.size(); }

    /// \return how many times `flush()` was called.
    [[nodiscard]] std::size_t flushes() const noexcept { return flushes_; }

    /// \return whether nothing has been written.
    [[nodiscard]] bool empty() const noexcept { return lines_.empty(); }

    /// Discard everything recorded, including the flush count.
    void clear() noexcept {
        lines_.clear();
        all_.clear();
        flushes_ = 0;
    }

   private:
    std::vector<std::string> lines_;
    std::string all_;
    std::size_t flushes_ = 0;
};

}  // namespace pico_logging::testing
