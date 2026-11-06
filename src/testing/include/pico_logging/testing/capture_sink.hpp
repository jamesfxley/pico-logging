// SPDX-License-Identifier: MIT
#pragma once

/// \file capture_sink.hpp
/// A Sink that records what was logged, for tests to assert on (FR-9, FR-10,
/// FR-11). It allocates freely (AR-4).
///
/// `allocation_test.cpp` and the firmware probe keep their own sinks: one needs
/// to provably allocate nothing, the other to compile without `<string>`.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "pico_logging/logging/sinks.hpp"

namespace pico_logging::testing {

/// Records every line written to it, whole and in order, as copies: the view it
/// is handed does not survive the call (FR-9).
class CaptureSink final : public Sink {
   public:
    /// Records one line, never truncating or dropping it.
    ///
    /// \param text the line, including its trailing newline.
    void write(std::string_view text) override {
        lines_.emplace_back(text);
        all_.append(text);
    }

    /// Counts a flush.
    void flush() override { ++flushes_; }

    /// \return every line written, concatenated, for asserting on content.
    [[nodiscard]] const std::string& str() const noexcept { return all_; }

    /// \return one entry per `write()` call, for asserting on framing (FR-10).
    [[nodiscard]] const std::vector<std::string>& lines() const noexcept {
        return lines_;
    }

    /// \return the most recent line, or an empty view if nothing was written.
    [[nodiscard]] std::string_view last() const noexcept {
        return lines_.empty() ? std::string_view{}
                              : std::string_view{lines_.back()};
    }

    /// \return how many times `write()` was called.
    [[nodiscard]] std::size_t writes() const noexcept { return lines_.size(); }

    /// \return how many times `flush()` was called.
    [[nodiscard]] std::size_t flushes() const noexcept { return flushes_; }

    /// \return whether nothing has been written.
    [[nodiscard]] bool empty() const noexcept { return lines_.empty(); }

    /// Discards everything recorded, including the flush count.
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
