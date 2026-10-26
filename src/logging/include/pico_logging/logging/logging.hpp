// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iterator>
#include <limits>
#include <source_location>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "pico_logging/logging/sinks.hpp"

/// \file logging.hpp
/// Level-filtered PICO_LOG_* stream macros for host and firmware builds.
///
/// Each statement emits one line — timestamp, colored level label, file:line,
/// message — through the pluggable Sink installed via set_sink() (stderr by
/// default). The log path never allocates: the line is staged in a fixed
/// stack buffer, and a message that overflows it is cut with a "..." marker.
///
/// Timestamps come from std::chrono::system_clock; on firmware that counts
/// from boot unless the wall clock has been synchronized (see pico-time).

namespace pico_logging {

/// The ceiling that applies when neither LOGGING_GLOBAL_LEVEL nor
/// PICO_LOGGING_DEFAULT_CEILING is defined (DBG).
/// Its value does not depend on any macro, so it is safe as a constant: every
/// translation unit agrees about it.
inline constexpr int LOGGING_DEFAULT_LEVEL = 4;

/// Divisor splitting a millisecond count into the seconds and fractional
/// milliseconds a timestamp prints separately.
inline constexpr int LOGGING_MILLISECONDS_PER_SECOND = 1000;
/// Base of the hand-rolled digit emission in format_timestamp.
inline constexpr int LOGGING_DECIMAL_RADIX = 10;
/// Full timestamp "YYYY-MM-DD HH:MM:SS.mmm" (23 chars) + NUL, rounded up.
inline constexpr std::size_t TIMESTAMP_BUFFER_SIZE = 32;
/// Seconds-resolution part "YYYY-MM-DD HH:MM:SS" (19 chars) + NUL.
inline constexpr std::size_t TIMESTAMP_SECONDS_SIZE = 24;

/// Digits format_timestamp emits after the decimal point, derived from the
/// divisor its emission loop starts at rather than restated, so the two cannot
/// drift.
constexpr std::size_t timestamp_millisecond_digits() {
    std::size_t digits = 0;
    for (int divisor = LOGGING_MILLISECONDS_PER_SECOND / LOGGING_DECIMAL_RADIX;
         divisor > 0; divisor /= LOGGING_DECIMAL_RADIX) {
        ++digits;
    }
    return digits;
}

/// Bytes format_timestamp appends after the seconds field: the '.' and the
/// digits. The guard admitting that write is expressed in terms of this rather
/// than of the gap between two documentation constants, so the bytes written
/// and the check that admits them cannot drift apart (CXX-06.3).
inline constexpr std::size_t TIMESTAMP_FRACTION_SIZE =
    1 + timestamp_millisecond_digits();

static_assert(TIMESTAMP_SECONDS_SIZE + TIMESTAMP_FRACTION_SIZE <=
                  TIMESTAMP_BUFFER_SIZE,
              "the timestamp buffer must hold the seconds field and the "
              "fractional part together");

/// Reserved for the assembled header (timestamp, level, file:line). A file
/// name too long for what is left is cut from the front and marked, so the
/// line number and the separator always land (BR-4); the alternative is
/// allocating on the log path, which this library does not do.
inline constexpr std::size_t LOG_HEADER_SIZE = 256;
/// Per-line message accumulation buffer; longer messages are truncated.
inline constexpr std::size_t LOG_BUFFER_CAPACITY = 256;
/// Marks every cut this library makes to a line, in the message (BR-2) and in
/// the file name (BR-4) alike.
inline constexpr std::string_view LOG_CUT_MARKER = "...";
/// Scratch for a single formatted number: the widest result this library
/// produces for any type it formats fits.
inline constexpr std::size_t NUMERIC_FORMAT_SIZE = 32;

/// Digits after the decimal point, in both notations (FR-5).
inline constexpr int FLOAT_DECIMAL_DIGITS = 6;

/// 10^FLOAT_DECIMAL_DIGITS, derived rather than restated so the two cannot
/// drift.
constexpr double float_decimal_scale() {
    double scale = 1.0;
    for (int digit = 0; digit < FLOAT_DECIMAL_DIGITS; ++digit) {
        scale *= static_cast<double>(LOGGING_DECIMAL_RADIX);
    }
    return scale;
}

/// Ten raised to FLOAT_DECIMAL_DIGITS. The fixed-point formatter multiplies by
/// this to turn a fractional part into the integer it prints.
inline constexpr double FLOAT_DECIMAL_SCALE = float_decimal_scale();

/// A magnitude at or above this renders in exponent notation. The fixed form
/// multiplies by FLOAT_DECIMAL_SCALE, and the resulting integer must stay
/// inside the range where a double represents every integer exactly, or the
/// value handed to llround cannot name the digits to print.
inline constexpr double FLOAT_FIXED_MAX = 1e9;

/// A non-zero magnitude below this renders in exponent notation: the fixed
/// form would print it as all zeros, which is not a number anyone can act on.
inline constexpr double FLOAT_FIXED_MIN = 1e-4;

/// Every integer up to 2^53 is exactly representable as a double; past it,
/// consecutive integers share a representation and the rendered digits stop
/// being the ones the caller supplied (CXX-11.2 binds the knob to the limit).
inline constexpr double DOUBLE_EXACT_INTEGER_LIMIT = 9007199254740992.0;
static_assert(
    FLOAT_FIXED_MAX * FLOAT_DECIMAL_SCALE <= DOUBLE_EXACT_INTEGER_LIMIT,
    "the fixed-notation range must stay inside exact integer doubles");
static_assert(FLOAT_FIXED_MIN < 1.0 && FLOAT_FIXED_MAX > 1.0,
              "1.0 must render in fixed notation");

/// The widest result format_float produces, over every double: sign, ten
/// integer digits after a rounding carry past FLOAT_FIXED_MAX, the point, and
/// FLOAT_DECIMAL_DIGITS places — "-1000000000.000000". A buffer shorter than
/// this is rejected rather than filled with a prefix (CXX-04.5).
inline constexpr std::size_t FLOAT_FORMAT_MAX = 18;
static_assert(FLOAT_FORMAT_MAX <= NUMERIC_FORMAT_SIZE,
              "write_float's buffer must satisfy format_float's minimum");

/// Severity levels, most severe first. A statement fires when its level is
/// <= both the compile-time ceiling (PICO_LOGGING_EFFECTIVE_LEVEL, checked in
/// the macro) and get_level() (runtime).
/// ERR/DBG rather than ERROR/DEBUG: those names are taken by common macros
/// (e.g. NDEBUG-style build flags and SDK headers).
///
/// The base type is fixed at one byte: a Level is stored in every LogLine, and
/// the comparisons against the ceiling are widened to int anyway.
enum class Level : std::uint8_t {
    NONE = 0,
    ERR = 1,
    WARN = 2,
    INFO = 3,
    DBG = 4,
    TRACE = 5,
};

/// ANSI escape selecting red, used for Level::ERR.
inline constexpr const char* COLOR_RED = "\033[31m";
/// ANSI escape selecting yellow, used for Level::WARN.
inline constexpr const char* COLOR_YELLOW = "\033[33m";
/// ANSI escape selecting green, used for Level::INFO.
inline constexpr const char* COLOR_GREEN = "\033[32m";
/// ANSI escape selecting cyan, used for Level::DBG.
inline constexpr const char* COLOR_CYAN = "\033[36m";
/// ANSI escape selecting blue, used for Level::TRACE.
inline constexpr const char* COLOR_BLUE = "\033[34m";
/// ANSI escape returning the terminal to its default attributes. Emitted after
/// every coloured field, so a truncated line cannot leave a terminal stained.
inline constexpr const char* COLOR_RESET = "\033[0m";

/// The fixed-width name a level prints as. Level::NONE and any value outside
/// the enumeration render as "NONE" rather than being rejected: a logger that
/// throws away a line because its level is out of range loses the diagnostic
/// that was being reported.
constexpr const char* label_for(Level level) noexcept {
    switch (level) {
        case Level::ERR:
            return "ERROR";
        case Level::WARN:
            return "WARN";
        case Level::INFO:
            return "INFO";
        case Level::DBG:
            return "DEBUG";
        case Level::TRACE:
            return "TRACE";
        case Level::NONE:
        default:
            return "NONE";
    }
}

/// The ANSI colour escape a level prints in. Pairs with COLOR_RESET, which the
/// caller must emit afterwards. Out-of-range values take COLOR_RESET, for the
/// same reason label_for() does not reject them.
constexpr const char* color_for(Level level) noexcept {
    switch (level) {
        case Level::ERR:
            return COLOR_RED;
        case Level::WARN:
            return COLOR_YELLOW;
        case Level::INFO:
            return COLOR_GREEN;
        case Level::DBG:
            return COLOR_CYAN;
        case Level::TRACE:
            return COLOR_BLUE;
        case Level::NONE:
        default:
            return COLOR_RESET;
    }
}

/// Strips directory components (either separator style) from a path, leaving
/// just the file name — keeps log headers short on deep source trees.
inline const char* filename_only(const char* path) noexcept {
    if (path == nullptr) {
        return "";
    }

    // The *later* of the two separators, not whichever style is present
    // first, so a mixed path such as "C:\src\lib/deep\file.cpp" splits at its
    // last separator of either kind. Both results point into `path`, so
    // comparing them is defined.
    const char* last_forward = std::strrchr(path, '/');
    const char* last_backward = std::strrchr(path, '\\');
    const char* candidate = std::max(last_forward, last_backward);
    return candidate != nullptr ? std::next(candidate) : path;
}

namespace detail {

/// A double's exponent bits. NaN and infinity are classified by bit pattern
/// rather than isnan and isinf, which a fast-math translation unit folds away
/// and clang warns on even where no float is logged; a bit pattern means the
/// same under every floating-point flag.
inline constexpr std::uint64_t DOUBLE_EXPONENT = 0x7FF0'0000'0000'0000ULL;
/// A double's mantissa bits.
inline constexpr std::uint64_t DOUBLE_MANTISSA = 0x000F'FFFF'FFFF'FFFFULL;

/// Whether `value` is a NaN, by its bit pattern.
inline bool is_nan(double value) {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    return (bits & DOUBLE_EXPONENT) == DOUBLE_EXPONENT &&
           (bits & DOUBLE_MANTISSA) != 0U;
}

/// Whether `value` is an infinity of either sign, by its bit pattern.
inline bool is_inf(double value) {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    return (bits & DOUBLE_EXPONENT) == DOUBLE_EXPONENT &&
           (bits & DOUBLE_MANTISSA) == 0U;
}

/// FR-5: whether this translation unit keeps IEEE floating-point semantics.
/// Under -ffinite-math-only (and -ffast-math) the compiler may assume no
/// infinity or NaN exists, so `inf` and `nan` cannot be promised; under
/// -fassociative-math or -fno-signed-zeros (both in
/// -funsafe-math-optimizations) the residual that makes the digits correctly
/// rounded is folded away and -0 loses its sign. Logging a float is refused
/// under each flag the compiler names: GCC names all four, and
/// -fassociative-math takes effect only alongside -fno-signed-zeros; clang
/// names only the first two, so a clang build with the last two is not
/// refused. Templated on
/// the logged type so the refusal fires only where one is logged.
template <typename T>
inline constexpr bool IEEE_FLOAT_SEMANTICS =
#if defined(__FAST_MATH__) ||                                  \
    (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__) || \
    defined(__ASSOCIATIVE_MATH__) || defined(__NO_SIGNED_ZEROS__)
    sizeof(T) == 0;
#else
    sizeof(T) != 0;
#endif

}  // namespace detail

namespace detail {

/// Appends `value` in decimal at `length`, zero-padded to at least
/// `min_digits`, and advances `length`. Writes nothing past the buffer.
inline void append_uint(std::span<char> buffer, std::size_t& length,
                        std::uint64_t value, int min_digits) {
    // 2^64 - 1 is 20 digits; nothing this function is given is wider.
    static constexpr int MAX_DIGITS = 20;
    std::array<char, MAX_DIGITS> digits{};

    int count = 0;
    if (value == 0) {
        digits[0] = '0';
        count = 1;
    }
    while (value != 0 && count < MAX_DIGITS) {
        digits[static_cast<std::size_t>(count)] = static_cast<char>(
            '0' + static_cast<int>(value % LOGGING_DECIMAL_RADIX));
        ++count;
        value /= LOGGING_DECIMAL_RADIX;
    }

    while (count < min_digits && count < MAX_DIGITS) {
        digits[static_cast<std::size_t>(count)] = '0';
        ++count;
    }

    while (count > 0 && length < buffer.size()) {
        --count;
        buffer[length] = digits[static_cast<std::size_t>(count)];
        ++length;
    }
}

/// The part `product = a * b` dropped: `a * b` is exactly `product + residual`.
///
/// Dekker's two-product. Splitting each operand into halves narrow enough that
/// every partial product is exact lets the rounded-away part be recovered by
/// arithmetic the same width as the inputs — no `fma`, so no libm call and no
/// dependence on how well a given target implements one. Valid while no
/// intermediate overflows, which the fixed range and the [1, 10) normalisation
/// both guarantee.
///
/// This is what makes the printed digits the correctly rounded digits of
/// `value` rather than of an intermediate: `magnitude * FLOAT_DECIMAL_SCALE`
/// is itself a rounded quantity, and above 1e5 that rounding is enough to
/// carry the last decimal place the wrong way.
inline double product_residual(double left, double right, double product) {
    // 2^27 + 1: splits a 53-bit significand into two halves of at most 26
    // bits, so each partial product below fits a double exactly.
    constexpr double SPLITTER = 134217729.0;

    const double left_split = left * SPLITTER;
    const double left_high = left_split - (left_split - left);
    const double left_low = left - left_high;
    const double right_split = right * SPLITTER;
    const double right_high = right_split - (right_split - right);
    const double right_low = right - right_high;

    // Left to right, as Dekker's proof requires: each partial sum stays
    // representable only in this order.
    return (((left_high * right_high - product) + (left_high * right_low)) +
            (left_low * right_high)) +
           (left_low * right_low);
}

/// The product rounded to an integer, correctly for the value it came from.
///
/// \param product the scaled value, itself rounded to a double.
/// \param residual what that rounding dropped, from product_residual().
/// \return the nearest integer to `product + residual`, halves away from zero.
///
/// llround only sees the rounded product, and the part it does not see can sit
/// on the other side of a half. The two are compared separately, never summed:
/// the sum rounds, and can land on the boundary it is compared with. llround
/// leaves the offset within half a step and a whole multiple of the product's
/// ulp, and the residual is at most half that ulp, so the exact excess reaches
/// the boundary only when the offset is exactly on it, and then the residual's
/// sign decides.
inline auto correctly_rounded(double product, double residual) {
    constexpr double ROUNDING_BOUNDARY = 0.5;
    auto rounded = std::llround(product);
    const double offset = product - static_cast<double>(rounded);
    if (offset == ROUNDING_BOUNDARY && residual >= 0.0) {
        ++rounded;
    } else if (offset == -ROUNDING_BOUNDARY && residual < 0.0) {
        --rounded;
    }
    return rounded;
}

/// Formats `value` into `buffer`, returning a view of what it wrote (FR-5).
///
/// Hand-rolled rather than std::to_chars or snprintf, and neither choice is
/// about elegance:
///
///   - snprintf reaches newlib's _dtoa_r -> _Balloc -> _malloc_r, which
///     allocates on the log path (AR-1).
///   - std::to_chars for a double takes the cross-compiled probe to about four
///     times its size. libstdc++'s shortest-round-trip path calls Ryu's
///     fixed-notation renderer, so its lookup tables are reachable and survive
///     --gc-sections. Measured, not assumed: tests/firmware/measurements.md.
///
/// The domain is restricted so the result is provable rather than clever:
/// inside [FLOAT_FIXED_MIN, FLOAT_FIXED_MAX) the scaled value is an integer a
/// double represents exactly, and the rounding into it is corrected by the
/// residual of its own multiplication, so the digits printed are the correctly
/// rounded digits of `value`. Outside it the value is normalised to [1, 10)
/// first, which costs precision in the last places — exponent notation is
/// documented as approximate, and a subnormal, needing hundreds of scaling
/// steps, is the worst case.
///
/// `buffer` must hold at least FLOAT_FORMAT_MAX characters. A shorter one
/// yields an empty view: a prefix of a number is indistinguishable from a
/// complete one and reads as a value orders of magnitude out (CXX-04.5).
inline std::string_view format_float(double value, std::span<char> buffer) {
    if (buffer.size() < FLOAT_FORMAT_MAX) {
        return {};
    }

    std::size_t length = 0;
    const auto put = [&](char character) {
        if (length < buffer.size()) {
            buffer[length] = character;
            ++length;
        }
    };
    const auto put_all = [&](std::string_view text) {
        for (const char character : text) {
            put(character);
        }
    };

    // NaN has no sign worth reporting, and comparing it leads nowhere.
    if (is_nan(value)) {
        put_all("nan");
        return {buffer.data(), length};
    }
    // Sign first, from signbit rather than a comparison, so negative zero
    // keeps the sign that distinguishes it.
    if (std::signbit(value)) {
        put('-');
    }
    double magnitude = std::fabs(value);
    if (is_inf(magnitude)) {
        put_all("inf");
        return {buffer.data(), length};
    }

    const bool use_exponent =
        magnitude != 0.0 &&
        (magnitude >= FLOAT_FIXED_MAX || magnitude < FLOAT_FIXED_MIN);
    int exponent = 0;
    if (use_exponent) {
        // Normalise into [1, 10). Bounded: a double's decimal exponent spans
        // roughly -324 to 308, so neither loop runs more than ~324 times.
        while (magnitude >= static_cast<double>(LOGGING_DECIMAL_RADIX)) {
            magnitude /= static_cast<double>(LOGGING_DECIMAL_RADIX);
            ++exponent;
        }
        while (magnitude < 1.0) {
            magnitude *= static_cast<double>(LOGGING_DECIMAL_RADIX);
            --exponent;
        }
    }

    // std::llround rather than casting (x + 0.5) to an integer: that addition
    // is not exact for every x, so the classic idiom rounds some values the
    // wrong way. llround rounds half away from zero, which is the rule FR-5
    // states, and `magnitude` is non-negative so the result cannot be either.
    const auto scale = static_cast<std::uint64_t>(FLOAT_DECIMAL_SCALE);
    const double product = magnitude * FLOAT_DECIMAL_SCALE;
    // llround only ever saw the rounded product, and the rounding it did not
    // see is the whole of the last decimal place: half an ulp at 1e15 is 1/16
    // of a digit step. Recovering the dropped part and re-deciding the
    // boundary makes the result the correctly rounded one — without it the
    // last place disagrees for a few per cent of magnitudes above 1e8.
    const auto rounded = correctly_rounded(
        product, product_residual(magnitude, FLOAT_DECIMAL_SCALE, product));

    auto scaled = static_cast<std::uint64_t>(rounded);

    // Rounding can carry 9.9999995 up to 10.000000, which is outside [1, 10)
    // and would print two integer digits where the format promises one.
    if (use_exponent &&
        scaled >= static_cast<std::uint64_t>(LOGGING_DECIMAL_RADIX) * scale) {
        scaled /= LOGGING_DECIMAL_RADIX;
        ++exponent;
    }

    append_uint(buffer, length, scaled / scale, 1);
    put('.');
    append_uint(buffer, length, scaled % scale, FLOAT_DECIMAL_DIGITS);

    if (use_exponent) {
        put('e');
        put(exponent < 0 ? '-' : '+');
        const auto magnitude_of_exponent =
            static_cast<std::uint64_t>(exponent < 0 ? -exponent : exponent);
        // Two digits minimum, matching the conventional printf rendering; a
        // three-digit exponent widens rather than truncating.
        append_uint(buffer, length, magnitude_of_exponent, 2);
    }

    return {buffer.data(), length};
}

/// View of the NUL-terminated string in `data`, bounded by `capacity`. Reads
/// at most `capacity` bytes, so it is safe over an array whose contents stop
/// short of its extent and over one with no terminator at all.
inline std::string_view bounded_view(const char* data,
                                     std::size_t capacity) noexcept {
    if (data == nullptr || capacity == 0) {
        return {};
    }
    const void* terminator = std::memchr(data, '\0', capacity);
    const std::size_t length =
        terminator != nullptr ? static_cast<std::size_t>(
                                    static_cast<const char*>(terminator) - data)
                              : capacity;
    return {data, length};
}

}  // namespace detail

/// Runtime log level, shared by every translation unit (default Level::INFO).
/// set_level() stores what it is given; the compile-time ceiling is a separate,
/// per-translation-unit filter applied by the PICO_LOG_* macros.
Level get_level() noexcept;
/// Set the runtime log level. Stores what it is given, with no cap against any
/// compile-time ceiling (OR-3): the ceiling is per translation unit, and a cap
/// applied here would be the ceiling of whichever unit compiled sinks.cpp.
void set_level(Level level) noexcept;

/// Runtime check only: true if `level` passes the current runtime level. This
/// cannot fold in the compile-time ceiling — an inline function that read it
/// would differ between translation units (see PICO_LOGGING_EFFECTIVE_LEVEL).
/// PICO_LOG_ENABLED_<LEVEL> is the combined check.
inline bool is_enabled(Level level) noexcept {
    return static_cast<int>(level) <= static_cast<int>(get_level());
}

/// Global sink all log lines are written to (StderrSink by default). The
/// caller keeps ownership and must keep the sink alive until it is replaced;
/// set_sink(nullptr) restores the default.
///
/// The pointer itself is atomic, so a concurrent log statement sees either the
/// old sink or the new one. Nothing more is synchronized: replacing a sink
/// while another context is inside the old one's write() is a lifetime problem
/// this cannot solve. Install sinks during startup, before other contexts log.
Sink* get_sink() noexcept;
/// Install the global sink, or restore StderrSink with nullptr. The caller
/// keeps ownership; see get_sink() for the lifetime and concurrency rules this
/// deliberately does not solve.
void set_sink(Sink* sink) noexcept;

namespace detail {

/// Formats "YYYY-MM-DD HH:MM:SS.mmm" from the local system clock into
/// `buffer`, returning a view of what it wrote. `buffer` must hold at least
/// TIMESTAMP_BUFFER_SIZE bytes; a shorter one, or a clock the platform cannot
/// convert to local time, yields an empty view. Allocates nothing.
///
/// Not part of the public surface (CXX-05.7): it is reachable only because
/// the line assembly below is inline.
inline std::string_view format_timestamp(std::span<char> buffer) {
    if (buffer.size() < TIMESTAMP_BUFFER_SIZE) {
        return {};
    }

    const auto current_time_point = std::chrono::system_clock::now();
    const auto current_time_seconds =
        std::chrono::system_clock::to_time_t(current_time_point);
    std::tm local_tm{};

    // A failed conversion leaves local_tm zeroed, which strftime renders as a
    // plausible-looking 1900 date. Track it so a broken clock produces an
    // empty timestamp rather than a confidently wrong one.
    bool have_local_time = false;
#if defined(_WIN32)
    have_local_time = localtime_s(&local_tm, &current_time_seconds) == 0;
#else
    have_local_time = localtime_r(&current_time_seconds, &local_tm) != nullptr;
#endif
    if (!have_local_time) {
        return {};
    }

    // "%Y-%m-%d %H:%M:%S" needs 20 bytes. strftime returns 0 with unspecified
    // buffer contents when it does not fit, so nothing may be read on 0.
    const std::size_t seconds_length = std::strftime(
        buffer.data(), buffer.size(), "%Y-%m-%d %H:%M:%S", &local_tm);
    if (seconds_length == 0) {
        return {};
    }

    // A time_point before the epoch gives a negative remainder; bring it back
    // into [0, 1000) so the digits below are always the ones intended.
    const auto raw_milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time_point.time_since_epoch())
            .count() %
        LOGGING_MILLISECONDS_PER_SECOND;
    const int millisecond_component = static_cast<int>(
        raw_milliseconds < 0
            ? raw_milliseconds + LOGGING_MILLISECONDS_PER_SECOND
            : raw_milliseconds);

    // ".mmm", emitted digit by digit rather than through snprintf: the
    // varargs path is what pulls newlib's allocating formatter onto the log
    // path (CXX-11.1), and a fixed three-digit field has nothing to gain from
    // a format string.
    std::size_t length = seconds_length;
    if (buffer.size() - length < TIMESTAMP_FRACTION_SIZE) {
        return {buffer.data(), length};
    }
    buffer[length++] = '.';
    for (int divisor = LOGGING_MILLISECONDS_PER_SECOND / LOGGING_DECIMAL_RADIX;
         divisor > 0; divisor /= LOGGING_DECIMAL_RADIX) {
        buffer[length++] = static_cast<char>(
            '0' + ((millisecond_component / divisor) % LOGGING_DECIMAL_RADIX));
    }
    return {buffer.data(), length};
}

}  // namespace detail

/// Digits in the widest line number std::source_location can report.
inline constexpr std::size_t LINE_NUMBER_DIGITS_MAX =
    std::numeric_limits<std::uint_least32_t>::digits10 + 1;

/// Separates the file name from the line number in the header.
inline constexpr std::string_view LOCATION_SEPARATOR = ":";
/// Separates the header from the message.
inline constexpr std::string_view HEADER_TERMINATOR = ": ";

/// The widest text that follows the file name in the header.
inline constexpr std::size_t LOCATION_SUFFIX_SIZE = LOCATION_SEPARATOR.size() +
                                                    LINE_NUMBER_DIGITS_MAX +
                                                    HEADER_TERMINATOR.size();

/// The widest header less its file name: timestamp, " [", the widest colour
/// and label, the reset, "] ", and the location suffix. Everything else in
/// the header is bounded, so the file name is the only field that can be cut.
constexpr std::size_t log_header_fixed_max() {
    std::size_t widest_color = 0;
    std::size_t widest_label = 0;
    for (const Level level : {Level::NONE, Level::ERR, Level::WARN, Level::INFO,
                              Level::DBG, Level::TRACE}) {
        widest_color =
            std::max(widest_color, std::string_view(color_for(level)).size());
        widest_label =
            std::max(widest_label, std::string_view(label_for(level)).size());
    }
    return TIMESTAMP_BUFFER_SIZE + 2 + widest_color + widest_label +
           std::string_view(COLOR_RESET).size() + 2 + LOCATION_SUFFIX_SIZE;
}

/// See log_header_fixed_max().
inline constexpr std::size_t LOG_HEADER_FIXED_MAX = log_header_fixed_max();

static_assert(LOG_HEADER_FIXED_MAX + LOG_CUT_MARKER.size() < LOG_HEADER_SIZE,
              "the header must hold every fixed field, the cut marker, and at "
              "least one byte of the file name");

/// One log statement: accumulates `<<` operands in a fixed stack buffer and
/// writes the finished line (header + message + newline) to the sink when it
/// goes out of scope at the end of the statement.
///
/// The two filters differ in what they cost. Above the compile-time ceiling
/// the whole statement is discarded by the macro, operands included. Below the
/// ceiling but above the runtime level, the LogLine is still constructed and
/// each `<<` is a cheap no-op — but the operand expressions are evaluated, so
/// guard an expensive one with PICO_LOG_ENABLED. Inserters exist for literals,
/// char*/const char*, anything convertible to std::string_view (including
/// std::string), bool, char, every integral width (signed and unsigned), and
/// floating point; other types render as "<?>". Messages that overflow the
/// buffer are truncated with a "..." marker, never allocated.
class LogLine {
   public:
    /// Begin a line at `level`, attributed to `location`. Captures whether the
    /// runtime level admits it once, here, so each subsequent `<<` is a single
    /// branch on a stored flag. Not called directly — log_stream() and the
    /// PICO_LOG_* macros construct it.
    LogLine(Level level, const std::source_location& location)
        : enabled_(is_enabled(level)), level_(level), location_(location) {}

    // A LogLine is meaningful only as the temporary a statement builds and
    // destroys, so it is neither copyable nor movable (CXX-05.1). Guaranteed
    // copy elision lets log_stream() return one by value regardless.
    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;
    LogLine(LogLine&&) = delete;
    LogLine& operator=(LogLine&&) = delete;

    ~LogLine() {
        if (enabled_) {
            flush_and_finalize();
        }
    }

    /// Append `value` to the line and return the line, so `<<` chains. A
    /// disabled line still evaluates `value` — that is the caller's expression,
    /// not this function's choice — but appends nothing. A type with no
    /// inserter renders as "<?>" rather than failing to compile, so adding a
    /// field to a logged struct cannot break a build.
    template <typename T>
    LogLine& operator<<(const T& value) {
        if (!enabled_) {
            return *this;
        }

        // Trait-based dispatch, not a fixed type list, so every integral type
        // formats whatever the fixed-width aliases name on a given platform
        // (on arm-none-eabi int32_t is `long`, uint32_t `unsigned long`). bool
        // and char are matched before the integral branches so they format as
        // text and a character rather than a number.
        if constexpr (std::is_array_v<T> &&
                      std::is_same_v<std::remove_extent_t<T>, char>) {
            // Length is the distance to the first NUL *within* the extent, not
            // the extent itself. Deduction through `const T&` strips top-level
            // const, so this branch takes every char array -- a scratch buffer
            // filled by ipaddr_ntoa_r as much as a literal -- and the bytes
            // past its NUL may be indeterminate (CXX-04.5, CXX-12.8). An array
            // with no NUL renders whole.
            write_text(detail::bounded_view(static_cast<const char*>(value),
                                            std::extent_v<T>));
        } else if constexpr (std::is_same_v<T, const char*> ||
                             std::is_same_v<T, char*>) {
            // Explicit (and null-guarded) before the generic string_view
            // branch, since constructing string_view from a null pointer is UB.
            write_text(value != nullptr ? std::string_view(value)
                                        : std::string_view("(null)"));
        } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
            // Before the string_view branch, which nullptr also converts to:
            // a view built from it reads address zero (FR-7).
            write_str("(null)");
        } else if constexpr (std::is_same_v<T, bool>) {
            write_str(value ? std::string_view("true")
                            : std::string_view("false"));
        } else if constexpr (std::is_same_v<T, char>) {
            write_text(std::string_view(&value, 1));
        } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T> &&
                             sizeof(T) <= sizeof(std::int64_t)) {
            write_number(static_cast<std::int64_t>(value));
        } else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T> &&
                             sizeof(T) <= sizeof(std::uint64_t)) {
            write_number(static_cast<std::uint64_t>(value));
        } else if constexpr (std::is_floating_point_v<T>) {
            write_floating(value);
        } else if constexpr (std::is_convertible_v<T, std::string_view>) {
            // std::string_view, std::string, and any string_view-convertible
            // type — without this header having to include <string>.
            write_text(std::string_view(value));
        } else {
            // BR-2: through write_str, so a placeholder cut at capacity
            // carries the marker like any other text.
            write_str("<?>");
        }
        return *this;
    }

   private:
    // FR-3: a long double wider than double renders through double where it
    // fits, and as the placeholder where the cast would print a different
    // value -- judged by what the cast produces: a finite value that becomes
    // infinite, or a non-zero one that becomes zero. Infinity and NaN pass
    // through.
    template <typename T>
    void write_floating(T value) {
        static_assert(detail::IEEE_FLOAT_SEMANTICS<T>,
                      "logging a float needs IEEE floating-point semantics "
                      "(FR-5): compile this file without -ffast-math, "
                      "-ffinite-math-only, -funsafe-math-optimizations, "
                      "-fassociative-math or -fno-signed-zeros");
        const auto narrowed = static_cast<double>(value);
        if constexpr (sizeof(T) > sizeof(double)) {
            if ((std::isfinite(value) && detail::is_inf(narrowed)) ||
                (value != static_cast<T>(0) && narrowed == 0.0)) {
                write_str("<?>");
                return;
            }
        }
        write_float(narrowed);
    }

    // FR-1: message text, with every control byte escaped, so one statement is
    // one line whatever the text carries, and the backslash doubled, so an
    // escape cannot be told from text that spells one. UTF-8 passes through. A
    // newline in text from outside would end the record and begin one that
    // names any file and level it likes. The prefix the logger writes, its
    // colour codes included, does not come through here.
    void write_text(std::string_view text) {
        // Narrowed with remove_prefix and remove_suffix, never substr():
        // substr's range check can throw, and its out_of_range path links the
        // exception and formatting machinery into a firmware image that never
        // throws.
        while (!text.empty()) {
            std::size_t plain = 0;
            while (plain < text.size() && is_printable(text[plain])) {
                ++plain;
            }
            std::string_view head = text;
            head.remove_suffix(text.size() - plain);
            write_str(head);
            if (plain == text.size()) {
                return;
            }
            write_escape(static_cast<unsigned char>(text[plain]));
            text.remove_prefix(plain + 1);
        }
    }

    static bool is_printable(char ch) {
        const auto byte = static_cast<unsigned char>(ch);
        return byte >= FIRST_PRINTABLE && byte != DELETE_BYTE && ch != '\\';
    }

    // Writes the escape and records where it lies, so a later cut can keep it
    // whole or drop it whole (BR-2): a partial escape reads as text that spells
    // one. The span is recorded from the escape's length before it is written,
    // and the two latest are kept: a rewind is at most LOG_CUT_MARKER's three
    // bytes and an escape at least two, so no third escape can be reached.
    void write_escape(unsigned char byte) {
        std::array<char, 4> escape{'\\', '\0', '\0', '\0'};
        std::size_t length = 2;
        switch (byte) {
            case '\n':
                escape[1] = 'n';
                break;
            case '\r':
                escape[1] = 'r';
                break;
            case '\t':
                escape[1] = 't';
                break;
            case '\\':
                escape[1] = '\\';
                break;
            default: {
                constexpr std::string_view HEX = "0123456789abcdef";
                constexpr unsigned NIBBLE_BITS = 4U;
                constexpr unsigned NIBBLE_MASK = 0x0FU;
                escape = {'\\', 'x', HEX[byte >> NIBBLE_BITS],
                          HEX[byte & NIBBLE_MASK]};
                length = escape.size();
                break;
            }
        }
        std::string_view text(escape.data(), escape.size());
        text.remove_suffix(escape.size() - length);
        previous_escape_ = last_escape_;
        last_escape_ = {.start = used(), .end = used() + length};
        write_str(text);
    }

    [[nodiscard]] std::size_t used() const {
        return LOG_BUFFER_CAPACITY - buffer_.remaining();
    }

    // Moves a rewind that landed inside either of the two latest escapes back
    // to that escape's start.
    void keep_escapes_whole() {
        for (const EscapeSpan& span : {last_escape_, previous_escape_}) {
            if (used() > span.start && used() < span.end) {
                buffer_.shrink_by(used() - span.start);
            }
        }
    }

    static constexpr unsigned char FIRST_PRINTABLE = 0x20U;
    static constexpr unsigned char DELETE_BYTE = 0x7FU;

    void write_str(std::string_view str) {
        if (truncated_) {
            return;
        }
        if (str.size() <= buffer_.remaining()) {
            buffer_.append(str);
            return;
        }
        // Won't fit: keep the prefix that leaves room for the marker, append
        // the marker exactly once, and ignore everything after this line.
        //
        // The marker must land whole, or an overlong line reads as complete
        // to anything matching on it (CXX-07.6). With fewer bytes left than
        // the marker there is nothing to reserve, so the marker makes room by
        // rewinding into what is already written, which is bounded by the
        // buffer being larger than the marker.
        const std::size_t keep =
            buffer_.remaining() > LOG_CUT_MARKER.size()
                ? buffer_.remaining() - LOG_CUT_MARKER.size()
                : 0;
        std::string_view prefix = str;
        prefix.remove_suffix(str.size() - keep);
        buffer_.append(prefix);
        if (buffer_.remaining() < LOG_CUT_MARKER.size()) {
            buffer_.shrink_by(LOG_CUT_MARKER.size() - buffer_.remaining());
            keep_escapes_whole();
        }
        buffer_.append(LOG_CUT_MARKER);
        truncated_ = true;
    }

    // FR-3: the two normalized integer widths are formatted by the module.
    template <typename T>
    void write_number(T value);

    // Floating point does not go through std::to_chars. Its shortest-round-trip
    // path in libstdc++ reaches Ryu's fixed-notation renderer, whose lookup
    // tables survive --gc-sections, and a consumer would pay for them the
    // moment it logs one float. detail::format_float allocates nothing and
    // costs a small fraction of that, at the price of the restricted domain it
    // documents (FR-5). tests/firmware/measurements.md carries the figures.
    void write_float(double value) {
        std::array<char, NUMERIC_FORMAT_SIZE> temp{};
        write_str(detail::format_float(value, temp));
    }

    void flush_and_finalize() {
        Sink* sink = get_sink();
        if (sink == nullptr) {
            return;
        }

        // Header: timestamp [LEVEL] file:line:
        std::array<char, TIMESTAMP_BUFFER_SIZE> timestamp{};
        buffer_.append_header(detail::format_timestamp(timestamp));
        buffer_.append_header(" [");
        buffer_.append_header(color_for(level_));
        buffer_.append_header(label_for(level_));
        buffer_.append_header(COLOR_RESET);
        buffer_.append_header("] ");

        std::array<char, NUMERIC_FORMAT_SIZE> line_number{};
        const std::to_chars_result formatted = std::to_chars(
            line_number.data(), line_number.data() + line_number.size(),
            location_.line());
        const std::string_view line =
            formatted.ec == std::errc{}
                ? std::string_view(line_number.data(),
                                   static_cast<std::size_t>(formatted.ptr -
                                                            line_number.data()))
                : std::string_view("?");

        // The file name takes what the rest of the header leaves, so the line
        // number and the separator before the message always land (BR-4).
        buffer_.append_file_name(
            filename_only(location_.file_name()),
            LOCATION_SEPARATOR.size() + line.size() + HEADER_TERMINATOR.size());
        buffer_.append_header(LOCATION_SEPARATOR);
        buffer_.append_header(line);
        buffer_.append_header(HEADER_TERMINATOR);

        // Exactly one write per line, then one flush (FR-2), so a second
        // context cannot interleave its own line between a header and a body.
        sink->write(buffer_.finish_line());
        sink->flush();
    }

    // Fixed-capacity line buffer — no heap, no NUL terminator kept (the views
    // it hands out carry an explicit length). append() copies only what fits.
    //
    // One array holds the whole line. The header is reserved at the front and
    // filled at flush time, the message accumulates behind it, and
    // finish_line() closes the gap between them. Assembling into a second
    // buffer instead would put a further LOG_HEADER_SIZE + LOG_BUFFER_CAPACITY
    // bytes on the stack of whichever context is logging — which on firmware
    // is an lwIP callback.
    class Buffer {
        std::array<char, LOG_HEADER_SIZE + LOG_BUFFER_CAPACITY + 1> data_{};
        std::size_t header_size_ = 0;
        std::size_t size_ = 0;

       public:
        void append(std::string_view str) {
            const std::size_t to_copy = std::min(str.size(), remaining());
            std::copy_n(str.begin(), to_copy,
                        data_.begin() + static_cast<std::ptrdiff_t>(
                                            LOG_HEADER_SIZE + size_));
            size_ += to_copy;
        }

        /// Discards the last `count` message bytes, so a caller can make room
        /// for something that must land whole. Clamped: it never underflows
        /// past an empty buffer.
        void shrink_by(std::size_t count) { size_ -= std::min(count, size_); }

        [[nodiscard]] std::size_t remaining() const {
            return LOG_BUFFER_CAPACITY - size_;
        }

        /// Appends to the reserved header prefix. Bounded rather than trusted:
        /// what does not fit in LOG_HEADER_SIZE is not written, although
        /// LOG_HEADER_FIXED_MAX establishes that only a file name can be
        /// long enough to reach this.
        void append_header(std::string_view str) {
            const std::size_t to_copy =
                std::min(str.size(), LOG_HEADER_SIZE - header_size_);
            std::copy_n(
                str.begin(), to_copy,
                data_.begin() + static_cast<std::ptrdiff_t>(header_size_));
            header_size_ += to_copy;
        }

        /// Appends `name`, leaving `reserve` bytes of the header free behind
        /// it. A name that does not fit keeps its end -- the part that tells
        /// two files apart -- behind LOG_CUT_MARKER (BR-4).
        void append_file_name(std::string_view name, std::size_t reserve) {
            const std::size_t used = header_size_ + reserve;
            const std::size_t room =
                used < LOG_HEADER_SIZE ? LOG_HEADER_SIZE - used : 0;
            if (name.size() <= room) {
                append_header(name);
                return;
            }
            if (room < LOG_CUT_MARKER.size()) {
                return;
            }
            // remove_prefix, not substr: substr throws on a position it cannot
            // prove in range, and that alone links libstdc++'s exception
            // machinery into a freestanding image.
            name.remove_prefix(name.size() - (room - LOG_CUT_MARKER.size()));
            append_header(LOG_CUT_MARKER);
            append_header(name);
        }

        /// Closes the gap the reservation left, terminates the line with a
        /// newline, and returns the whole thing as one view.
        [[nodiscard]] std::string_view finish_line() {
            // Destination is strictly below the source, so the ranges may
            // overlap safely. Equal is excluded by the guard, where there is
            // no gap to close and nothing to do.
            if (header_size_ < LOG_HEADER_SIZE) {
                std::copy_n(
                    data_.begin() +
                        static_cast<std::ptrdiff_t>(LOG_HEADER_SIZE),
                    size_,
                    data_.begin() + static_cast<std::ptrdiff_t>(header_size_));
            }
            const std::size_t line_length = header_size_ + size_;
            data_[line_length] = '\n';
            return {data_.data(), line_length + 1};
        }
    };

    Buffer buffer_{};
    bool enabled_ = false;
    bool truncated_ = false;

    // The two latest escapes' spans, in message bytes, for keep_escapes_whole.
    struct EscapeSpan {
        std::size_t start = 0;
        std::size_t end = 0;
    };

    EscapeSpan last_escape_{};
    EscapeSpan previous_escape_{};
    Level level_;
    std::source_location location_;
};

/// Factory behind the PICO_LOG_* macros. Defaulting source_location here
/// (rather than inside LogLine) captures the caller's file:line.
template <Level level>
LogLine log_stream(
    std::source_location location = std::source_location::current()) {
    return {level, location};
}

// A LogLine only ever names the statement's own temporary, so there is nothing
// for a move to mean. Pinned here because deleting a move operation is easy to
// undo by accident while "fixing" a build error.
static_assert(!std::is_move_constructible_v<LogLine>,
              "LogLine must not be movable: a move drops the message");
static_assert(!std::is_move_assignable_v<LogLine>,
              "LogLine must not be movable: a move drops the message");

}  // namespace pico_logging

/// Compile-time level ceiling (OR-1): a statement above it is discarded by
/// `if constexpr` in the PICO_LOG_* macro below, so neither the statement nor
/// its operands are evaluated. Set it project-wide with the PICO_LOGGING_LEVEL
/// CMake option, which defines PICO_LOGGING_DEFAULT_CEILING (0 = none ...
/// 5 = trace) as a PUBLIC compile definition so the library and its consumers
/// agree (OR-4), and override it for one target by defining
/// LOGGING_GLOBAL_LEVEL there.
///
/// Deliberately a macro rather than a constant (OR-2, CXX-10.3, CXX-11.2). The
/// value varies per translation unit, and every entity that reads it must
/// therefore be one the linker never sees: a macro is expanded in place and
/// never linked, whereas an `inline constexpr` variable, an `inline` function
/// or a template instantiation carrying this value gives two
/// differently-configured translation units two definitions of one entity —
/// an ODR violation the linker resolves by arbitrarily discarding one.
///
/// The default expands to the constant rather than repeating the literal, so
/// there is exactly one place the default 4 is written.
///
/// A target's own LOGGING_GLOBAL_LEVEL wins over PICO_LOGGING_DEFAULT_CEILING,
/// which the PICO_LOGGING_LEVEL CMake option propagates to every target (OR-4).
/// Two names rather than one, so an override is a second macro and not a
/// redefinition of the first.
#ifdef LOGGING_GLOBAL_LEVEL
#define PICO_LOGGING_EFFECTIVE_LEVEL (LOGGING_GLOBAL_LEVEL)
#elif defined(PICO_LOGGING_DEFAULT_CEILING)
#define PICO_LOGGING_EFFECTIVE_LEVEL (PICO_LOGGING_DEFAULT_CEILING)
#else
#define PICO_LOGGING_EFFECTIVE_LEVEL (::pico_logging::LOGGING_DEFAULT_LEVEL)
#endif

// Usage: PICO_LOG_INFO << "message " << value;  — one line per statement,
// flushed to the sink when the temporary LogLine dies at the semicolon.
//
// The compile-time ceiling is applied here rather than inside the library, so
// the whole statement — operands included — is discarded by `if constexpr`
// when it cannot fire. This is what keeps PICO_LOGGING_EFFECTIVE_LEVEL out of
// every linked entity (CXX-10.3): the macro is expanded per translation unit
// and never linked, so two translation units may disagree about it safely.
//
// The discarded branch is the empty one, so a trailing `else` at the call site
// still binds to the caller's own `if`:
//
//     if (ready) PICO_LOG_INFO << "up"; else recover();
//
// binds `recover()` to `if (ready)`, because the inner `if constexpr` already
// has an `else`. A statement macro cannot be used as an expression.
// Written out one per level rather than through a shared parameterised helper.
// A function-like macro would be a second, internal name in every consumer's
// global namespace, and the five below are the whole of the public surface.
/// Log at Level::ERR. A statement, not an expression: it expands to an
/// `if constexpr` whose empty branch is the discarded one.
#define PICO_LOG_ERROR                                           \
    if constexpr (static_cast<int>(::pico_logging::Level::ERR) > \
                  PICO_LOGGING_EFFECTIVE_LEVEL) {                \
    } else                                                       \
        ::pico_logging::log_stream<::pico_logging::Level::ERR>()

/// Log at Level::WARN. See PICO_LOG_ERROR for the expansion.
#define PICO_LOG_WARN                                             \
    if constexpr (static_cast<int>(::pico_logging::Level::WARN) > \
                  PICO_LOGGING_EFFECTIVE_LEVEL) {                 \
    } else                                                        \
        ::pico_logging::log_stream<::pico_logging::Level::WARN>()

/// Log at Level::INFO. See PICO_LOG_ERROR for the expansion.
#define PICO_LOG_INFO                                             \
    if constexpr (static_cast<int>(::pico_logging::Level::INFO) > \
                  PICO_LOGGING_EFFECTIVE_LEVEL) {                 \
    } else                                                        \
        ::pico_logging::log_stream<::pico_logging::Level::INFO>()

/// Log at Level::DBG. See PICO_LOG_ERROR for the expansion.
#define PICO_LOG_DEBUG                                           \
    if constexpr (static_cast<int>(::pico_logging::Level::DBG) > \
                  PICO_LOGGING_EFFECTIVE_LEVEL) {                \
    } else                                                       \
        ::pico_logging::log_stream<::pico_logging::Level::DBG>()

/// Log at Level::TRACE. See PICO_LOG_ERROR for the expansion.
#define PICO_LOG_TRACE                                             \
    if constexpr (static_cast<int>(::pico_logging::Level::TRACE) > \
                  PICO_LOGGING_EFFECTIVE_LEVEL) {                  \
    } else                                                         \
        ::pico_logging::log_stream<::pico_logging::Level::TRACE>()

/// Both filters as one expression, one per level, for guarding work that is
/// expensive enough to be worth skipping outside a log statement:
///
///     if (PICO_LOG_ENABLED_DBG) { PICO_LOG_DEBUG << expensive_summary(); }
///
/// Macros rather than a function for the same reason as the ceiling itself: a
/// `constexpr` function or template reading PICO_LOGGING_EFFECTIVE_LEVEL would
/// be the ODR carrier that OR-2 exists to remove. One per level rather than one
/// taking the level, so no internal function-like macro enters the consumer's
/// global namespace; each pairs with the statement macro of the same name.
#define PICO_LOG_ENABLED_ERR                         \
    (static_cast<int>(::pico_logging::Level::ERR) <= \
         PICO_LOGGING_EFFECTIVE_LEVEL &&             \
     ::pico_logging::is_enabled(::pico_logging::Level::ERR))

/// Both filters for Level::WARN. See PICO_LOG_ENABLED_ERR.
#define PICO_LOG_ENABLED_WARN                         \
    (static_cast<int>(::pico_logging::Level::WARN) <= \
         PICO_LOGGING_EFFECTIVE_LEVEL &&              \
     ::pico_logging::is_enabled(::pico_logging::Level::WARN))

/// Both filters for Level::INFO. See PICO_LOG_ENABLED_ERR.
#define PICO_LOG_ENABLED_INFO                         \
    (static_cast<int>(::pico_logging::Level::INFO) <= \
         PICO_LOGGING_EFFECTIVE_LEVEL &&              \
     ::pico_logging::is_enabled(::pico_logging::Level::INFO))

/// Both filters for Level::DBG. See PICO_LOG_ENABLED_ERR.
#define PICO_LOG_ENABLED_DBG                         \
    (static_cast<int>(::pico_logging::Level::DBG) <= \
         PICO_LOGGING_EFFECTIVE_LEVEL &&             \
     ::pico_logging::is_enabled(::pico_logging::Level::DBG))

/// Both filters for Level::TRACE. See PICO_LOG_ENABLED_ERR.
#define PICO_LOG_ENABLED_TRACE                         \
    (static_cast<int>(::pico_logging::Level::TRACE) <= \
         PICO_LOGGING_EFFECTIVE_LEVEL &&               \
     ::pico_logging::is_enabled(::pico_logging::Level::TRACE))
