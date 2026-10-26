// SPDX-License-Identifier: MIT
// FR-5. The hand-rolled float formatter, at every edge of its domain.
//
// It exists because the two obvious alternatives are both unacceptable here:
// snprintf("%g") allocates on newlib (AR-1), and std::to_chars for a double
// takes the cross-compiled probe to about four times its size, because
// libstdc++'s shortest-round-trip path reaches Ryu's fixed-notation lookup
// tables. This one is under 2 KiB and allocates nothing
// (tests/firmware/measurements.md).
//
// The price is a restricted domain, and a restricted domain is only safe if
// its edges are pinned. That is what this file is for.

#include <gtest/gtest.h>

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "pico_logging/logging/logging.hpp"
#include "pico_logging/testing/capture_sink.hpp"

namespace {

/// Formats into a full-size scratch buffer and returns the result as a string,
/// so a failing expectation prints something readable.
std::string format(double value) {
    std::array<char, pico_logging::NUMERIC_FORMAT_SIZE> buffer{};
    const std::string_view formatted =
        pico_logging::detail::format_float(value, buffer);
    return std::string(formatted);
}

}  // namespace

// A value the caller cannot act on must not look like one they can.
TEST(FloatFormatTest, SpecialValuesRenderAsThemselves) {
    EXPECT_EQ(format(std::numeric_limits<double>::quiet_NaN()), "nan");
    EXPECT_EQ(format(std::numeric_limits<double>::signaling_NaN()), "nan");
    EXPECT_EQ(format(std::numeric_limits<double>::infinity()), "inf");
    EXPECT_EQ(format(-std::numeric_limits<double>::infinity()), "-inf");
}

// Negative zero keeps its sign. It is the difference between "underflowed from
// below" and "is zero", which is exactly the kind of thing a log is read for.
TEST(FloatFormatTest, ZeroKeepsItsSign) {
    EXPECT_EQ(format(0.0), "0.000000");
    EXPECT_EQ(format(-0.0), "-0.000000");
}

TEST(FloatFormatTest, FixedNotationRendersTheDigitsItWasGiven) {
    EXPECT_EQ(format(1.0), "1.000000");
    EXPECT_EQ(format(-1.5), "-1.500000");
    // Inside the fixed range the scaled value is an integer a double holds
    // exactly, so these digits are not an approximation.
    EXPECT_EQ(format(123.456789), "123.456789");
    EXPECT_EQ(format(-123.456789), "-123.456789");
}

// Rounding is half away from zero, applied to the value the caller supplied --
// which is the double, not the decimal text of the literal.
//
// The distinction is the whole point of these cases. The double nearest
// 0.1234565 is 0.12345649999999999679..., strictly below the decimal half, so
// the correctly rounded sixth place is 6 and not 7. A literal that looks like
// an exact half almost never is one.
TEST(FloatFormatTest, RoundsHalfAwayFromZeroAtTheLastDigit) {
    EXPECT_EQ(format(1.0 / 3.0), "0.333333");
    // 0.6666... must round up rather than truncate.
    EXPECT_EQ(format(2.0 / 3.0), "0.666667");
    EXPECT_EQ(format(-2.0 / 3.0), "-0.666667");

    EXPECT_EQ(format(0.1234565), "0.123456");
    EXPECT_EQ(format(0.1234564), "0.123456");

    // A carry that runs past the decimal point and into the integer part.
    EXPECT_EQ(format(1.9999996), "2.000000");
    EXPECT_EQ(format(1.9999995), "1.999999");
    EXPECT_EQ(format(-1.9999996), "-2.000000");
}

// A real tie, which needs a value 10^-6 can halve exactly: 1/128 scales to
// 7812.5 on the nose. Only a dyadic rational can land there -- 10^-6 carries a
// factor of 5^6 -- so a decimal literal will not produce one, and this is the
// only shape that exercises the half-away-from-zero rule as a rule.
TEST(FloatFormatTest, AnExactHalfRoundsAwayFromZero) {
    EXPECT_EQ(format(1.0 / 128.0), "0.007813");
    EXPECT_EQ(format(-1.0 / 128.0), "-0.007813");
    EXPECT_EQ(format(3.0 / 128.0), "0.023438");
    EXPECT_EQ(format(-3.0 / 128.0), "-0.023438");
}

// A product that lands exactly on a half after scaling, with a residual below
// it: the double 0.0039765 is just under 0.0039765, so it rounds down. Adding
// the product's offset and the residual in floating point rounds the sum back
// onto the half and the digit goes up.
TEST(FloatFormatTest, AHalfWithANegativeResidualRoundsDown) {
    EXPECT_EQ(format(0.0039765), "0.003976");
    EXPECT_EQ(format(-0.0039765), "-0.003976");
}

// The last place is the correctly rounded one across the whole fixed range,
// not only where the arithmetic happens to be forgiving.
//
// Each literal below denotes a double slightly under an exact decimal half, so
// half-away-from-zero must round it DOWN. Scaling by 10^6 in double rounds the
// product up to the half first, so without the product's residual carried into
// the decision the digit goes the wrong way. One per affected decade, because
// the error grows with magnitude: it is absent below 1e5.
TEST(FloatFormatTest, TheLastPlaceIsCorrectlyRoundedAcrossTheFixedRange) {
    EXPECT_EQ(format(294936.4972605), "294936.497260");
    EXPECT_EQ(format(351522.9986155), "351522.998615");
    EXPECT_EQ(format(6231170.5011655), "6231170.501165");
    EXPECT_EQ(format(70377111.6564405), "70377111.656440");
    EXPECT_EQ(format(319925668.4765525), "319925668.476552");
    EXPECT_EQ(format(-319925668.4765525), "-319925668.476552");
}

// A carry that runs the whole width of the fixed range and past its maximum,
// which is where the widest result comes from.
TEST(FloatFormatTest, ACarryRunsPastTheFixedMaximum) {
    EXPECT_EQ(format(0.9999999), "1.000000");
    EXPECT_EQ(format(999999999.9999999), "1000000000.000000");
    EXPECT_EQ(format(-999999999.9999999), "-1000000000.000000");
}

// The claim FR-5 makes is about every value in the fixed range, and a handful
// of literals cannot establish that. This walks the whole range against a
// reference that is correctly rounded by definition.
//
// std::to_chars in fixed notation is the reference: correctly rounded by the
// standard, and the library may not use it for a double only because of what
// it drags into a firmware image, which is not a constraint on a host test.
//
// It rounds half to even where FR-5 rounds half away from zero, so exact ties
// are excluded. A tie is exactly a value whose product with 128 is an odd
// integer -- multiplying by a power of two is exact for every double, so that
// test is itself exact.
//
// The sweep is a deterministic stride rather than a random sample: it covers
// each decade evenly, it is the same set of values on every machine and every
// run, and a failure names a value that can be pasted straight into a case.
TEST(FloatFormatTest, MatchesACorrectlyRoundedReferenceAcrossTheFixedRange) {
    // The fixed range spans 1e-4 to 1e9; the sweep covers every decade of it.
    constexpr int LOWEST_DECADE = -4;
    constexpr int HIGHEST_DECADE = 9;
    constexpr int SAMPLES_PER_DECADE = 20000;
    // An irrational offset within each step, so samples never land on the
    // round decimal values that a stride alone would produce -- those are the
    // easy cases, and the interesting ones sit between them.
    constexpr double IRRATIONAL_OFFSET = 0.6180339887498949;
    constexpr double HALF_DETECTOR = 128.0;
    constexpr double EVEN_DIVISOR = 2.0;

    std::size_t compared = 0;

    for (int decade = LOWEST_DECADE; decade < HIGHEST_DECADE; ++decade) {
        const double low = std::pow(10.0, decade);
        const double high = std::pow(10.0, decade + 1);
        const double step = (high - low) / SAMPLES_PER_DECADE;

        for (int sample = 0; sample < SAMPLES_PER_DECADE; ++sample) {
            const double value = low + (step * (sample + IRRATIONAL_OFFSET));
            if (value < pico_logging::FLOAT_FIXED_MIN ||
                value >= pico_logging::FLOAT_FIXED_MAX) {
                continue;
            }
            const double halves = value * HALF_DETECTOR;
            if (halves == std::floor(halves) &&
                std::fmod(halves, EVEN_DIVISOR) != 0.0) {
                continue;  // an exact tie, where the two rules disagree
            }

            std::array<char, pico_logging::NUMERIC_FORMAT_SIZE> reference{};
            const std::to_chars_result rendered = std::to_chars(
                reference.data(), reference.data() + reference.size(), value,
                std::chars_format::fixed, pico_logging::FLOAT_DECIMAL_DIGITS);
            ASSERT_EQ(rendered.ec, std::errc{});

            ++compared;
            ASSERT_EQ(format(value),
                      std::string(reference.data(),
                                  static_cast<std::size_t>(rendered.ptr -
                                                           reference.data())))
                << "at "
                << std::setprecision(std::numeric_limits<double>::max_digits10)
                << value;
        }
    }

    EXPECT_GT(compared, static_cast<std::size_t>(SAMPLES_PER_DECADE *
                                                 (HIGHEST_DECADE - 1)));
}

// The stride above never lands near a rounding half: from 1e5 up, step * 1e6
// is a whole number, so every sample in a decade has the same sixth-place
// fraction. Rounding errors live beside a half, so this sweep stands on one --
// (floor(x * 1e6) + 0.5) / 1e6 for a sample x of each decade -- and on the
// doubles either side of it, which is where a dropped or mis-signed residual
// renders the wrong last digit.
TEST(FloatFormatTest, MatchesTheReferenceBesideEveryRoundingHalf) {
    constexpr int LOWEST_DECADE = -4;
    constexpr int HIGHEST_DECADE = 9;
    // Dense enough to reach the rare value where only the residual's sign
    // decides the digit: at this density the sweep fails an offset-plus-
    // residual comparison, which a tenth of it does not.
    constexpr int SAMPLES_PER_DECADE = 100000;
    constexpr int NEIGHBOURS = 4;
    constexpr double IRRATIONAL_OFFSET = 0.6180339887498949;
    constexpr double SCALE = 1e6;
    constexpr double HALF_DETECTOR = 128.0;
    constexpr double EVEN_DIVISOR = 2.0;

    std::size_t compared = 0;
    for (int decade = LOWEST_DECADE; decade < HIGHEST_DECADE; ++decade) {
        const double low = std::pow(10.0, decade);
        const double step =
            (std::pow(10.0, decade + 1) - low) / SAMPLES_PER_DECADE;
        for (int sample = 0; sample < SAMPLES_PER_DECADE; ++sample) {
            const double base = low + (step * (sample + IRRATIONAL_OFFSET));
            const double half = (std::floor(base * SCALE) + 0.5) / SCALE;
            double value = half;
            for (int i = 0; i < NEIGHBOURS; ++i) {
                value = std::nextafter(value, 0.0);
            }
            for (int i = 0; i <= 2 * NEIGHBOURS;
                 ++i, value = std::nextafter(value, HUGE_VAL)) {
                if (value < pico_logging::FLOAT_FIXED_MIN ||
                    value >= pico_logging::FLOAT_FIXED_MAX) {
                    continue;
                }
                const double halves = value * HALF_DETECTOR;
                if (halves == std::floor(halves) &&
                    std::fmod(halves, EVEN_DIVISOR) != 0.0) {
                    continue;  // an exact tie, where the two rules disagree
                }
                std::array<char, pico_logging::NUMERIC_FORMAT_SIZE> reference{};
                const std::to_chars_result rendered = std::to_chars(
                    reference.data(), reference.data() + reference.size(),
                    value, std::chars_format::fixed,
                    pico_logging::FLOAT_DECIMAL_DIGITS);
                ASSERT_EQ(rendered.ec, std::errc{});
                ++compared;
                ASSERT_EQ(format(value),
                          std::string(reference.data(),
                                      static_cast<std::size_t>(
                                          rendered.ptr - reference.data())))
                    << "at "
                    << std::setprecision(
                           std::numeric_limits<double>::max_digits10)
                    << value;
            }
        }
    }
    EXPECT_GT(compared, static_cast<std::size_t>(SAMPLES_PER_DECADE *
                                                 (HIGHEST_DECADE - 1)));
}

// A value below FLOAT_FIXED_MIN takes exponent notation rather than being
// flattened to zeros, which is the point of having a lower threshold at all:
// "5.000000e-07" says something, "0.000001" says almost nothing and "0.000000"
// says something false.
TEST(FloatFormatTest, SmallValuesKeepTheirMagnitudeRatherThanFlattening) {
    EXPECT_EQ(format(0.0000005), "5.000000e-07");
    EXPECT_EQ(format(0.0000004), "4.000000e-07");
}

// Boundary triple at the small end: below it the fixed form would print all
// zeros, so the notation changes.
TEST(FloatFormatTest, SwitchesToExponentBelowTheFixedMinimum) {
    EXPECT_EQ(format(pico_logging::FLOAT_FIXED_MIN), "0.000100");
    EXPECT_EQ(format(0.000100001), "0.000100");
    // Just under the threshold.
    EXPECT_EQ(format(9.999e-5), "9.999000e-05");
    EXPECT_EQ(format(1e-9), "1.000000e-09");
    EXPECT_EQ(format(-1e-9), "-1.000000e-09");
}

// Boundary triple at the large end: at and above it, the scaled value would
// leave the range where a double represents every integer exactly.
TEST(FloatFormatTest, SwitchesToExponentAtTheFixedMaximum) {
    EXPECT_EQ(format(pico_logging::FLOAT_FIXED_MAX - 1.0), "999999999.000000");
    EXPECT_EQ(format(pico_logging::FLOAT_FIXED_MAX), "1.000000e+09");
    EXPECT_EQ(format(1e15), "1.000000e+15");
    EXPECT_EQ(format(-1e15), "-1.000000e+15");
}

// Rounding can carry the normalised mantissa from 9.9999995 up to 10, which
// would print two integer digits where exponent notation promises one.
TEST(FloatFormatTest, ARoundingCarryRenormalisesTheExponent) {
    EXPECT_EQ(format(9.9999999e-9), "1.000000e-08");
    EXPECT_EQ(format(-9.9999999e-9), "-1.000000e-08");
    EXPECT_EQ(format(9.9999999e20), "1.000000e+21");
}

// A three-digit exponent widens the field rather than truncating it, and the
// extremes of the type stay legible.
TEST(FloatFormatTest, TheExtremesOfTheTypeRender) {
    EXPECT_EQ(format(std::numeric_limits<double>::max()), "1.797693e+308");
    EXPECT_EQ(format(std::numeric_limits<double>::min()), "2.225074e-308");
    // Subnormals need hundreds of scaling steps to normalise; the loop is
    // bounded and still arrives somewhere sensible.
    EXPECT_EQ(format(std::numeric_limits<double>::denorm_min()),
              "4.940656e-324");
}

// Every result fits the scratch the log path gives it, so the inserter never
// has to think about the formatter overflowing.
TEST(FloatFormatTest, EveryResultFitsTheNumericScratch) {
    const std::array<double, 8> extremes = {
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::denorm_min(),
        -std::numeric_limits<double>::denorm_min(),
        pico_logging::FLOAT_FIXED_MAX - 1.0,
        -(pico_logging::FLOAT_FIXED_MAX - 1.0),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),
    };
    for (const double value : extremes) {
        EXPECT_LE(format(value).size(), pico_logging::NUMERIC_FORMAT_SIZE)
            << "value rendered as " << format(value);
    }
}

// A buffer below the documented worst case is refused, not filled with a
// prefix. "123" for 12345.6 reads as a number two orders of magnitude out and
// nothing in the return value distinguishes it from a complete result, so an
// empty view is the only answer a caller cannot misread (CXX-04.5).
TEST(FloatFormatTest, ABufferBelowTheWorstCaseIsRefused) {
    for (std::size_t capacity = 0; capacity < pico_logging::FLOAT_FORMAT_MAX;
         ++capacity) {
        std::array<char, 32> guarded{};
        guarded.fill('#');
        const std::string_view formatted = pico_logging::detail::format_float(
            12345.6, std::span<char>(guarded.data(), capacity));

        EXPECT_TRUE(formatted.empty()) << "at capacity " << capacity;
        // Nothing was written at all, inside the span or past it.
        for (const char guard : guarded) {
            ASSERT_EQ(guard, '#') << "wrote into a buffer it refused, at "
                                     "capacity "
                                  << capacity;
        }
    }
}

// FLOAT_FORMAT_MAX is the worst case over every double, not an estimate: the
// widest result is a rounding carry past FLOAT_FIXED_MAX with a sign in front,
// and it fits exactly.
TEST(FloatFormatTest, TheWorstCaseFitsTheDocumentedMinimum) {
    std::array<char, pico_logging::FLOAT_FORMAT_MAX> exact{};
    const std::string_view widest =
        pico_logging::detail::format_float(-999999999.9999999, exact);

    EXPECT_EQ(widest, "-1000000000.000000");
    EXPECT_EQ(widest.size(), pico_logging::FLOAT_FORMAT_MAX);
}

// The inserter reaches the same formatter, and a float widens to double first
// rather than taking a different path. Read back through a sink, so a
// floating-point branch in LogLine that rendered anything else fails here.
TEST(FloatFormatTest, TheInserterRendersWhatTheFormatterDoes) {
    pico_logging::testing::CaptureSink sink;
    pico_logging::set_sink(&sink);
    const auto inserted = [&sink](auto value) {
        sink.clear();
        PICO_LOG_WARN << "[" << value << "]";
        const std::string& out = sink.str();
        const std::size_t open = out.rfind('[');
        const std::size_t close = out.rfind(']');
        return open != std::string::npos && close != std::string::npos
                   ? out.substr(open + 1, close - open - 1)
                   : out;
    };
    const float single = 1.5F;
    const double fraction = 0.0039765;
    const double large = 123456789.25;
    EXPECT_EQ(inserted(single), format(static_cast<double>(single)));
    EXPECT_EQ(inserted(fraction), format(fraction));
    EXPECT_EQ(inserted(large), format(large));
    EXPECT_EQ(inserted(single), "1.500000");
    pico_logging::set_sink(nullptr);
}
