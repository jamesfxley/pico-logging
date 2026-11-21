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
/// Level-filtered PICO_LOG_* stream macros. Each statement emits one line
/// through the Sink installed by set_sink(), stderr by default. Timestamps come
/// from std::chrono::system_clock, which on firmware counts from boot unless
/// the wall clock has been synchronised (see pico-time).

namespace pico_logging {

/// Ceiling when neither LOGGING_GLOBAL_LEVEL nor PICO_LOGGING_DEFAULT_CEILING
/// is defined (DBG).
inline constexpr int LOGGING_DEFAULT_LEVEL = 4;

/// Milliseconds in a second.
inline constexpr int LOGGING_MILLISECONDS_PER_SECOND = 1000;
/// Radix of the digits format_timestamp and format_float emit.
inline constexpr int LOGGING_DECIMAL_RADIX = 10;
/// Bytes for "YYYY-MM-DD HH:MM:SS.mmm" (23 characters) and a NUL, rounded up.
inline constexpr std::size_t TIMESTAMP_BUFFER_SIZE = 32;
/// Bytes for "YYYY-MM-DD HH:MM:SS" (19 characters) and a NUL, rounded up.
inline constexpr std::size_t TIMESTAMP_SECONDS_SIZE = 24;

/// Digits after the point in a timestamp, derived from the divisor
/// format_timestamp's loop starts at, so the two cannot drift.
constexpr std::size_t timestamp_millisecond_digits() {
    std::size_t digits = 0;
    for (int divisor = LOGGING_MILLISECONDS_PER_SECOND / LOGGING_DECIMAL_RADIX;
         divisor > 0; divisor /= LOGGING_DECIMAL_RADIX) {
        ++digits;
    }
    return digits;
}

/// Bytes format_timestamp appends after the seconds field: the '.' and the
/// digits. The write's guard uses it, so the two cannot drift.
inline constexpr std::size_t TIMESTAMP_FRACTION_SIZE =
    1 + timestamp_millisecond_digits();

static_assert(TIMESTAMP_SECONDS_SIZE + TIMESTAMP_FRACTION_SIZE <=
                  TIMESTAMP_BUFFER_SIZE,
              "the timestamp buffer must hold the seconds field and the "
              "fractional part together");

/// Bytes reserved for the header (timestamp, level, file:line). A file name too
/// long for it is cut from the front and marked (BR-4).
inline constexpr std::size_t LOG_HEADER_SIZE = 256;
/// Bytes of message a line holds; a longer message is cut (BR-1).
inline constexpr std::size_t LOG_BUFFER_CAPACITY = 256;
/// Marks a cut in the message (BR-2) and in the file name (BR-4).
inline constexpr std::string_view LOG_CUT_MARKER = "...";
/// Bytes of scratch for one formatted number; the widest result fits.
inline constexpr std::size_t NUMERIC_FORMAT_SIZE = 32;

/// Digits after the decimal point, in both notations (FR-5).
inline constexpr int FLOAT_DECIMAL_DIGITS = 6;

/// Derives FLOAT_DECIMAL_SCALE from FLOAT_DECIMAL_DIGITS, so they cannot drift.
constexpr double float_decimal_scale() {
    double scale = 1.0;
    for (int digit = 0; digit < FLOAT_DECIMAL_DIGITS; ++digit) {
        scale *= static_cast<double>(LOGGING_DECIMAL_RADIX);
    }
    return scale;
}

/// Ten raised to FLOAT_DECIMAL_DIGITS (FR-5).
inline constexpr double FLOAT_DECIMAL_SCALE = float_decimal_scale();

/// Magnitudes at or above this print in exponent notation (FR-5).
inline constexpr double FLOAT_FIXED_MAX = 1e9;

/// Smallest non-zero magnitude printed in fixed notation (FR-5).
inline constexpr double FLOAT_FIXED_MIN = 1e-4;

/// 2^53: every integer up to it is exact in a double.
inline constexpr double DOUBLE_EXACT_INTEGER_LIMIT = 9007199254740992.0;
static_assert(
    FLOAT_FIXED_MAX * FLOAT_DECIMAL_SCALE <= DOUBLE_EXACT_INTEGER_LIMIT,
    "the fixed-notation range must stay inside exact integer doubles");
static_assert(FLOAT_FIXED_MIN < 1.0 && FLOAT_FIXED_MAX > 1.0,
              "1.0 must render in fixed notation");

/// Characters in the widest format_float result, "-1000000000.000000" (FR-5).
inline constexpr std::size_t FLOAT_FORMAT_MAX = 18;
static_assert(FLOAT_FORMAT_MAX <= NUMERIC_FORMAT_SIZE,
              "write_float's buffer must satisfy format_float's minimum");

/// Severity levels, most severe first (OR-1). One byte, because every LogLine
/// stores one.
enum class Level : std::uint8_t {
    NONE = 0,
    ERR = 1,
    WARN = 2,
    INFO = 3,
    DBG = 4,
    TRACE = 5,
};

/// Colour for Level::ERR.
inline constexpr const char* COLOR_RED = "\033[31m";
/// Colour for Level::WARN.
inline constexpr const char* COLOR_YELLOW = "\033[33m";
/// Colour for Level::INFO.
inline constexpr const char* COLOR_GREEN = "\033[32m";
/// Colour for Level::DBG.
inline constexpr const char* COLOR_CYAN = "\033[36m";
/// Colour for Level::TRACE.
inline constexpr const char* COLOR_BLUE = "\033[34m";
/// Resets the terminal's attributes. Emitted after every coloured field, so a
/// truncated line cannot leave the terminal coloured.
inline constexpr const char* COLOR_RESET = "\033[0m";

/// The name a level prints as. Level::NONE and any value outside the
/// enumeration give "NONE" and are not rejected, so the diagnostic is not lost.
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

/// The colour escape a level prints in; the caller must emit COLOR_RESET
/// afterwards. Out-of-range values give COLOR_RESET, as label_for() does.
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

/// The path after its last '/' or '\\', or the whole path if it has neither.
/// A null path gives an empty string.
inline const char* filename_only(const char* path) noexcept {
    if (path == nullptr) {
        return "";
    }

    // The later separator wins, so "C:\src\lib/deep\file.cpp" splits at its
    // last. Both pointers address `path`, so comparing them is defined.
    const char* last_forward = std::strrchr(path, '/');
    const char* last_backward = std::strrchr(path, '\\');
    const char* candidate = std::max(last_forward, last_backward);
    return candidate != nullptr ? std::next(candidate) : path;
}

namespace detail {

/// A double's exponent bits. NaN and infinity are classified by bit pattern
/// because a fast-math unit folds isnan and isinf away and clang warns on them
/// even where no float is logged.
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

/// Whether IEEE floating-point semantics hold in this unit (FR-5). Dependent on
/// the logged type, so its static_assert fires only where a float is logged.
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
    // 2^64 - 1 has 20 digits.
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

/// What rounding dropped from `product = left * right`, by Dekker's
/// two-product, which needs no fma or libm call. Valid while no intermediate
/// overflows, as the fixed range and the [1, 10) normalisation guarantee
/// (FR-5).
inline double product_residual(double left, double right, double product) {
    // 2^27 + 1 splits a 53-bit significand into halves of at most 26 bits, so
    // each partial product is exact.
    constexpr double SPLITTER = 134217729.0;

    const double left_split = left * SPLITTER;
    const double left_high = left_split - (left_split - left);
    const double left_low = left - left_high;
    const double right_split = right * SPLITTER;
    const double right_high = right_split - (right_split - right);
    const double right_low = right - right_high;

    // Left to right: each partial sum is representable only in this order.
    return (((left_high * right_high - product) + (left_high * right_low)) +
            (left_low * right_high)) +
           (left_low * right_low);
}

/// The integer nearest `product + residual`, halves away from zero.
/// \param product the scaled value, rounded to a double.
/// \param residual what that rounding dropped, from product_residual().
inline auto correctly_rounded(double product, double residual) {
    constexpr double ROUNDING_BOUNDARY = 0.5;
    auto rounded = std::llround(product);
    // The offset and the residual are compared separately and never summed:
    // the sum rounds and can land on the boundary. The offset is a whole
    // multiple of the product's ulp and the residual at most half of one, so
    // the excess reaches the boundary only when the offset is on it, and then
    // the residual's sign decides.
    const double offset = product - static_cast<double>(rounded);
    if (offset == ROUNDING_BOUNDARY && residual >= 0.0) {
        ++rounded;
    } else if (offset == -ROUNDING_BOUNDARY && residual < 0.0) {
        --rounded;
    }
    return rounded;
}

/// Formats `value` into `buffer` and returns what it wrote (FR-5). The digits
/// are correctly rounded inside [FLOAT_FIXED_MIN, FLOAT_FIXED_MAX) and
/// approximate outside it. `buffer` must hold at least FLOAT_FORMAT_MAX
/// characters; a shorter one yields an empty view and not a prefix.
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

    // NaN carries no sign worth reporting.
    if (is_nan(value)) {
        put_all("nan");
        return {buffer.data(), length};
    }
    // Sign from signbit and not a comparison, so negative zero keeps it.
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
        // Normalise into [1, 10). A double's decimal exponent spans about -324
        // to 308, so neither loop runs more than ~324 times.
        while (magnitude >= static_cast<double>(LOGGING_DECIMAL_RADIX)) {
            magnitude /= static_cast<double>(LOGGING_DECIMAL_RADIX);
            ++exponent;
        }
        while (magnitude < 1.0) {
            magnitude *= static_cast<double>(LOGGING_DECIMAL_RADIX);
            --exponent;
        }
    }

    // std::llround and not a cast of (x + 0.5): the addition is inexact for
    // some x.
    // `magnitude` is non-negative, so the cast of `rounded` to std::uint64_t
    // below is exact.
    const auto scale = static_cast<std::uint64_t>(FLOAT_DECIMAL_SCALE);
    const double product = magnitude * FLOAT_DECIMAL_SCALE;
    const auto rounded = correctly_rounded(
        product, product_residual(magnitude, FLOAT_DECIMAL_SCALE, product));

    auto scaled = static_cast<std::uint64_t>(rounded);

    // A carry from 9.9999995 to 10.000000 leaves [1, 10) and would print two
    // integer digits.
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
        // At least two digits, as printf does; a three-digit exponent widens.
        append_uint(buffer, length, magnitude_of_exponent, 2);
    }

    return {buffer.data(), length};
}

/// View of `data` up to its first NUL or `capacity` bytes. It reads no further,
/// so an array with no terminator is safe (FR-4).
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
Level get_level() noexcept;
/// Sets the runtime level without applying the compile-time ceiling (OR-3).
void set_level(Level level) noexcept;

/// Whether `level` passes the runtime level alone, not the ceiling (OR-2).
/// PICO_LOG_ENABLED_<LEVEL> checks both.
inline bool is_enabled(Level level) noexcept {
    return static_cast<int>(level) <= static_cast<int>(get_level());
}

/// The global sink (StderrSink by default). The caller owns it and must keep it
/// alive until it is replaced. Replacing a sink while another context is inside
/// the old one's write() is not synchronised (CR-2), so install sinks during
/// start-up, before another context logs.
Sink* get_sink() noexcept;
/// Installs the global sink, or restores StderrSink with nullptr. The lifetime
/// and concurrency rules of get_sink() are not enforced here.
void set_sink(Sink* sink) noexcept;

namespace detail {

/// Formats "YYYY-MM-DD HH:MM:SS.mmm" local time into `buffer` and returns what
/// it wrote. `buffer` must hold at least TIMESTAMP_BUFFER_SIZE bytes; a shorter
/// one, or a clock the platform cannot convert to local time, yields an empty
/// view (FR-8). Allocates nothing.
inline std::string_view format_timestamp(std::span<char> buffer) {
    if (buffer.size() < TIMESTAMP_BUFFER_SIZE) {
        return {};
    }

    const auto current_time_point = std::chrono::system_clock::now();
    const auto current_time_seconds =
        std::chrono::system_clock::to_time_t(current_time_point);
    std::tm local_tm{};

    // A failed conversion leaves local_tm zeroed, which would print as a 1900
    // date (FR-8).
    bool have_local_time = false;
#if defined(_WIN32)
    have_local_time = localtime_s(&local_tm, &current_time_seconds) == 0;
#else
    have_local_time = localtime_r(&current_time_seconds, &local_tm) != nullptr;
#endif
    if (!have_local_time) {
        return {};
    }

    // strftime returns 0 with unspecified buffer contents when the text does
    // not fit, so nothing may be read on 0.
    const std::size_t seconds_length = std::strftime(
        buffer.data(), buffer.size(), "%Y-%m-%d %H:%M:%S", &local_tm);
    if (seconds_length == 0) {
        return {};
    }

    // A time_point before the epoch gives a negative remainder; fold it into
    // [0, 1000).
    const auto raw_milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time_point.time_since_epoch())
            .count() %
        LOGGING_MILLISECONDS_PER_SECOND;
    const int millisecond_component = static_cast<int>(
        raw_milliseconds < 0
            ? raw_milliseconds + LOGGING_MILLISECONDS_PER_SECOND
            : raw_milliseconds);

    // ".mmm", emitted digit by digit and not through snprintf (AR-1).
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

/// Digits in the widest std::source_location line number.
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

/// The widest header less its file name. Every other field is bounded, so the
/// file name is the only one that can be cut (BR-4).
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
/// writes the finished line to the sink when the statement ends.
///
/// Operands are evaluated whenever the compile-time ceiling admits the
/// statement, even if the runtime level does not, so guard an expensive one
/// with PICO_LOG_ENABLED_<LEVEL> (OR-1).
class LogLine {
   public:
    /// Begins a line at `level` for `location`, reading the runtime level once
    /// so each `<<` is one branch. Built by log_stream(), not called directly.
    LogLine(Level level, const std::source_location& location)
        : enabled_(is_enabled(level)), level_(level), location_(location) {}

    // Neither copyable nor movable: a LogLine exists only as its statement's
    // temporary. Guaranteed copy elision still lets log_stream() return one.
    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;
    LogLine(LogLine&&) = delete;
    LogLine& operator=(LogLine&&) = delete;

    ~LogLine() {
        if (enabled_) {
            flush_and_finalize();
        }
    }

    /// Appends `value` and returns the line. A disabled line evaluates `value`
    /// but appends nothing; a type with no inserter renders as "<?>" (FR-3).
    template <typename T>
    LogLine& operator<<(const T& value) {
        if (!enabled_) {
            return *this;
        }

        // By trait and not a fixed type list (FR-3). bool and char come before
        // the integral branches, so they do not format as numbers.
        if constexpr (std::is_array_v<T> &&
                      std::is_same_v<std::remove_extent_t<T>, char>) {
            // The bytes past the first NUL can be indeterminate (FR-4).
            write_text(detail::bounded_view(static_cast<const char*>(value),
                                            std::extent_v<T>));
        } else if constexpr (std::is_same_v<T, const char*> ||
                             std::is_same_v<T, char*>) {
            // Null-guarded and before the string_view branch (FR-7).
            write_text(value != nullptr ? std::string_view(value)
                                        : std::string_view("(null)"));
        } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
            // Before the string_view branch, which nullptr also converts to
            // (FR-7).
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
            // Covers std::string without including <string>.
            write_text(std::string_view(value));
        } else {
            // Through write_str, so a placeholder cut at capacity carries the
            // marker (BR-2).
            write_str("<?>");
        }
        return *this;
    }

   private:
    // A long double narrows to double unless that changes the value (finite to
    // infinite, non-zero to zero); those render as the placeholder (FR-3).
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

    // Escapes control bytes and doubles the backslash (FR-1). The header does
    // not come through here.
    void write_text(std::string_view text) {
        // remove_prefix and remove_suffix, not substr(): its out_of_range path
        // links exception machinery into a firmware image that never throws.
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

    // Records the escape's span before writing it, because write_str can cut
    // and rewind (BR-2). Only the two latest are kept: a rewind is at most
    // LOG_CUT_MARKER's three bytes and an escape at least two, so no third can
    // be reached.
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
        // Keep the prefix that leaves room for the marker, append it once and
        // ignore the rest. With fewer bytes left than the marker, rewind to
        // make room (BR-2); the rewind is bounded, as the buffer is larger.
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

    // The two normalised integer widths are formatted by the module (FR-3).
    template <typename T>
    void write_number(T value);

    // Floating point does not go through std::to_chars (FR-5).
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

        // The file name takes what the header and the separator leave, so the
        // line number always lands (BR-4).
        buffer_.append_file_name(
            filename_only(location_.file_name()),
            LOCATION_SEPARATOR.size() + line.size() + HEADER_TERMINATOR.size());
        buffer_.append_header(LOCATION_SEPARATOR);
        buffer_.append_header(line);
        buffer_.append_header(HEADER_TERMINATOR);

        // One write per line, then one flush (FR-2).
        sink->write(buffer_.finish_line());
        sink->flush();
    }

    // Fixed-capacity line buffer, with no heap and no NUL. One array holds the
    // line: the header is reserved at the front and filled at flush time, and
    // finish_line() closes the gap. A second assembly buffer would add
    // LOG_HEADER_SIZE + LOG_BUFFER_CAPACITY bytes of stack (AR-5).
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

        /// Discards the last `count` message bytes, clamped at empty.
        void shrink_by(std::size_t count) { size_ -= std::min(count, size_); }

        [[nodiscard]] std::size_t remaining() const {
            return LOG_BUFFER_CAPACITY - size_;
        }

        /// Appends to the header reservation; what exceeds LOG_HEADER_SIZE is
        /// dropped. Only a file name can reach it (LOG_HEADER_FIXED_MAX).
        void append_header(std::string_view str) {
            const std::size_t to_copy =
                std::min(str.size(), LOG_HEADER_SIZE - header_size_);
            std::copy_n(
                str.begin(), to_copy,
                data_.begin() + static_cast<std::ptrdiff_t>(header_size_));
            header_size_ += to_copy;
        }

        /// Appends `name`, leaving `reserve` header bytes free. A name that
        /// does not fit keeps its end behind LOG_CUT_MARKER (BR-4).
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
            // remove_prefix and not substr, as in write_text().
            name.remove_prefix(name.size() - (room - LOG_CUT_MARKER.size()));
            append_header(LOG_CUT_MARKER);
            append_header(name);
        }

        /// Closes the gap the reservation left, adds the newline and returns
        /// the line.
        [[nodiscard]] std::string_view finish_line() {
            // The destination is below the source, so overlapping ranges are
            // safe; equal means no gap to close.
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

/// Factory behind the PICO_LOG_* macros. Defaulting source_location here, not
/// in LogLine, captures the caller's file:line.
template <Level level>
LogLine log_stream(
    std::source_location location = std::source_location::current()) {
    return {level, location};
}

// Pins LogLine's deleted move operations, so a build fix cannot restore them.
static_assert(!std::is_move_constructible_v<LogLine>,
              "LogLine must not be movable: a move drops the message");
static_assert(!std::is_move_assignable_v<LogLine>,
              "LogLine must not be movable: a move drops the message");

}  // namespace pico_logging

/// Compile-time level ceiling: a statement above it is discarded, operands
/// included (OR-1). Set project-wide by the PICO_LOGGING_LEVEL CMake option, or
/// for one target by LOGGING_GLOBAL_LEVEL (docs/usage-guide.md, OR-4). A macro
/// because its value varies per translation unit (docs/architecture.md,
/// Filtering; OR-2).
#ifdef LOGGING_GLOBAL_LEVEL
#define PICO_LOGGING_EFFECTIVE_LEVEL (LOGGING_GLOBAL_LEVEL)
#elif defined(PICO_LOGGING_DEFAULT_CEILING)
#define PICO_LOGGING_EFFECTIVE_LEVEL (PICO_LOGGING_DEFAULT_CEILING)
#else
#define PICO_LOGGING_EFFECTIVE_LEVEL (::pico_logging::LOGGING_DEFAULT_LEVEL)
#endif

// The discarded branch is the empty one, so a trailing `else` still binds to
// the caller's own `if`:
//
//     if (ready) PICO_LOG_INFO << "up"; else recover();
//
// binds `recover()` to `if (ready)`, because the inner `if constexpr` already
// has an `else`.
//
// One macro per level and no shared helper, which would add an internal name
// to every consumer's global namespace.
/// Logs at Level::ERR. A statement, not an expression.
#define PICO_LOG_ERROR                                           \
    if constexpr (static_cast<int>(::pico_logging::Level::ERR) > \
                  PICO_LOGGING_EFFECTIVE_LEVEL) {                \
    } else                                                       \
        ::pico_logging::log_stream<::pico_logging::Level::ERR>()

/// Logs at Level::WARN.
#define PICO_LOG_WARN                                             \
    if constexpr (static_cast<int>(::pico_logging::Level::WARN) > \
                  PICO_LOGGING_EFFECTIVE_LEVEL) {                 \
    } else                                                        \
        ::pico_logging::log_stream<::pico_logging::Level::WARN>()

/// Logs at Level::INFO.
#define PICO_LOG_INFO                                             \
    if constexpr (static_cast<int>(::pico_logging::Level::INFO) > \
                  PICO_LOGGING_EFFECTIVE_LEVEL) {                 \
    } else                                                        \
        ::pico_logging::log_stream<::pico_logging::Level::INFO>()

/// Logs at Level::DBG.
#define PICO_LOG_DEBUG                                           \
    if constexpr (static_cast<int>(::pico_logging::Level::DBG) > \
                  PICO_LOGGING_EFFECTIVE_LEVEL) {                \
    } else                                                       \
        ::pico_logging::log_stream<::pico_logging::Level::DBG>()

/// Logs at Level::TRACE.
#define PICO_LOG_TRACE                                             \
    if constexpr (static_cast<int>(::pico_logging::Level::TRACE) > \
                  PICO_LOGGING_EFFECTIVE_LEVEL) {                  \
    } else                                                         \
        ::pico_logging::log_stream<::pico_logging::Level::TRACE>()

/// Both filters for Level::ERR, as one expression for guarding expensive work.
/// Macros and not a function, as the ceiling is (OR-2).
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
