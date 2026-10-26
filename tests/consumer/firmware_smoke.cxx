// SPDX-License-Identifier: MIT
// The installed package, consumed by a firmware program. It builds against the
// prefix `cmake --install` staged, links the exported modules and the SDK's
// standard library, and is never run: a probe image has no board to run on. The
// capture sink is host-only, so this program supplies a sink of its own.
#include <cstddef>
#include <string_view>

#include "pico/stdlib.h"
#include "pico_logging/pico_logging.hpp"

namespace {

class CountingSink final : public pico_logging::Sink {
   public:
    void write(std::string_view text) override { written_ += text.size(); }

    [[nodiscard]] std::size_t written() const { return written_; }

   private:
    std::size_t written_ = 0;
};

}  // namespace

int main() {
    stdio_init_all();
    CountingSink sink;
    pico_logging::set_sink(&sink);
    PICO_LOG_INFO << "installed package reachable from firmware";
    pico_logging::set_sink(nullptr);
    return sink.written() > 0 ? 0 : 1;
}
