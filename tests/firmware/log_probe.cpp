// SPDX-License-Identifier: MIT
// Cross-compiled probe for the firmware allocation gate (AR-1).
//
// Not a unit test: nothing here runs. It makes the linker resolve every
// formatting path this library can take, so the symbols and footprint gates
// declared in pico.toml can inspect the image and its objects.
//
// The `mode = "objects"` checks in pico.toml point at this probe's object
// directory and not the library's, which is load-bearing: format_timestamp and
// all of LogLine are header-inline and never land in sinks.cpp.o, so the same
// check against build-firmware/src would pass while inspecting none of that
// code. Moving the log statements out of this translation unit empties those
// checks without failing them, because the symbols gate fails only when it
// finds no objects at all.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "pico_logging/logging/logging.hpp"
#include "pico_logging/logging/ring_sink.hpp"
#include "pico_logging/logging/sinks.hpp"

namespace {

// A sink that touches no stdio, so the probe sees the formatting path and not
// newlib's buffering behind fwrite.
class NullSink final : public pico_logging::Sink {
   public:
    void write(std::string_view text) override { consumed_ += text.size(); }

    [[nodiscard]] std::size_t consumed() const { return consumed_; }

   private:
    std::size_t consumed_ = 0;
};

// RingSink's producer lock. The probe links no SDK library, so an atomic flag
// stands in for the critical_section an application supplies.
class SpinLock {
   public:
    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
        }
    }

    void unlock() { flag_.clear(std::memory_order_release); }

   private:
    std::atomic_flag flag_;
};

// Named operands, so the widths below read as coverage and not as arithmetic.
constexpr std::int8_t SMALL_SIGNED = -5;
constexpr std::int32_t LARGE_NEGATIVE = -2000000000;
constexpr unsigned SMALL_UNSIGNED = 7;
constexpr float SINGLE_PRECISION = 1.5F;
constexpr double RECURRING = 1.0 / 3.0;
constexpr long double WIDE = 2.5L;

}  // namespace

int main() {
    // Locals and not globals: nothing outlives this only function.
    NullSink null_sink;
    SpinLock lock;
    pico_logging::RingSink<4, SpinLock> ring_sink(null_sink, lock);

    pico_logging::set_sink(&ring_sink);
    pico_logging::set_level(pico_logging::Level::TRACE);

    const std::string_view view = "view";
    const char* const pointer = "pointer";

    struct Opaque {
        int field = 0;
    };

    // Every inserter branch: literal (a char array), char*, string_view, bool,
    // char, signed and unsigned integers of each width, nullptr, floating point
    // including long double, and the placeholder for a type with no inserter.
    PICO_LOG_ERROR << "literal " << view << ' ' << true << ' ' << SMALL_SIGNED
                   << ' ' << LARGE_NEGATIVE << ' ' << SMALL_UNSIGNED << ' '
                   << UINT64_MAX << ' ' << static_cast<const char*>(nullptr)
                   << ' ' << pointer << ' ' << nullptr << ' ' << Opaque{};
    PICO_LOG_WARN << SINGLE_PRECISION << ' ' << RECURRING << ' ' << WIDE;
    PICO_LOG_TRACE << "trace";

    ring_sink.drain();

    return static_cast<int>(null_sink.consumed() + ring_sink.dropped());
}
