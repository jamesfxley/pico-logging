// SPDX-License-Identifier: MIT
// Installing a sink, with the sink the guide assumes the reader has written.
#include <pico_logging/logging/logging.hpp>
#include <pico_logging/logging/sinks.hpp>
#include <string_view>

namespace doc_examples {
struct UartSink : pico_logging::Sink {
    void write(std::string_view text) override;
};

#include "guide_set_sink.inc"
}  // namespace doc_examples
