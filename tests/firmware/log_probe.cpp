// SPDX-License-Identifier: MIT
// Cross-compiled probe for the firmware allocation gate (AR-1).
//
// Not a unit test: nothing here is executed. It exists so the linker has to
// resolve every formatting path this library can take, and the gates can then
// ask what the resulting image and its objects contain. Three declarations in
// pico.toml take this one file as their subject:
//
//   the symbols gate, image mode, set allocating-formatter
//       Does the linked image define newlib's allocating formatter.
//   the symbols gate, objects mode, set c-heap-alloc, over the probe's .dir
//       Does this library's own inline code reach an allocator directly.
//   the footprint gate, with the RAM and flash budgets
//       Has a formatting choice added a step change of RAM or flash.
//
// The objects row is pointed at *this probe's* object directory rather than
// at the library's, and that is load-bearing: format_timestamp and the whole
// of LogLine are header-inline, so they never land in sinks.cpp.o and the same
// check against build-firmware/src would pass while inspecting none of the
// code it exists to inspect. Anything that moves the log statements out of
// this translation unit satisfies every claim above and empties that row --
// the symbols gate refuses only when it finds no objects at all, not when it
// finds objects with nothing of the library inlined into them.
//
// The defect all three stand against: formatting a float with snprintf("%g")
// reaches _dtoa_r -> _Balloc -> _malloc_r, which allocates whenever the bucket
// freelist is empty -- on the log path of a long-running image, which is
// exactly what "the log path never allocates" promises it does not do.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "pico_logging/logging/logging.hpp"
#include "pico_logging/logging/ring_sink.hpp"
#include "pico_logging/logging/sinks.hpp"

namespace {

// A sink that touches no stdio, so the probe measures the formatting path
// rather than the buffering newlib does behind fwrite.
class NullSink final : public pico_logging::Sink {
   public:
    void write(std::string_view text) override { consumed_ += text.size(); }

    [[nodiscard]] std::size_t consumed() const { return consumed_; }

   private:
    std::size_t consumed_ = 0;
};

// RingSink's producer lock. The probe links no SDK library, so this stands in
// for the critical_section an application supplies: an atomic flag, which is
// the part of that lock the formatting path itself pays for.
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

// Operands with named values, so the widths below read as the coverage they
// are rather than as arithmetic.
constexpr std::int8_t SMALL_SIGNED = -5;
constexpr std::int32_t LARGE_NEGATIVE = -2000000000;
constexpr unsigned SMALL_UNSIGNED = 7;
constexpr float SINGLE_PRECISION = 1.5F;
constexpr double RECURRING = 1.0 / 3.0;
constexpr long double WIDE = 2.5L;

}  // namespace

int main() {
    // Locals rather than globals: this is the only function, and a sink that
    // outlives it would buy nothing.
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
