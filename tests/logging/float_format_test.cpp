// SPDX-License-Identifier: MIT
// The hand-rolled float formatter, at every edge of its domain (FR-5).

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

/// Formats into a full-size scratch buffer and returns a string.
std::string format(double value) {
    std::array<char, pico_logging::NUMERIC_FORMAT_SIZE> buffer{};
    const std::string_view formatted =
        pico_logging::detail::format_float(value, buffer);
    return std::string(formatted);
}

}  // namespace

TEST(FloatFormatTest, SpecialValuesRenderAsThemselves) {
    EXPECT_EQ(format(std::numeric_limits<double>::quiet_NaN()), "nan");
    EXPECT_EQ(format(std::numeric_limits<double>::signaling_NaN()), "nan");
    EXPECT_EQ(format(std::numeric_limits<double>::infinity()), "inf");
    EXPECT_EQ(format(-std::numeric_limits<double>::infinity()), "-inf");
}

// The sign of zero tells an underflow from a true zero.
TEST(FloatFormatTest, ZeroKeepsItsSign) {
    EXPECT_EQ(format(0.0), "0.000000");
    EXPECT_EQ(format(-0.0), "-0.000000");
}

TEST(FloatFormatTest, FixedNotationRendersTheDigitsItWasGiven) {
    EXPECT_EQ(format(1.0), "1.000000");
    EXPECT_EQ(format(-1.5), "-1.500000");
    EXPECT_EQ(format(123.456789), "123.456789");
    EXPECT_EQ(format(-123.456789), "-123.456789");
}

// Rounding applies to the double and not to the decimal text of the literal:
// 0.1234565 is a half at the sixth place, but the double nearest it is below
// that half, so the sixth place is 6 and not 7 (FR-5).
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

// A real tie needs a dyadic rational: 1/128 scales to 7812.5 exactly, and a
// decimal literal cannot, since 10^-6 carries a factor of 5^6.
TEST(FloatFormatTest, AnExactHalfRoundsAwayFromZero) {
    EXPECT_EQ(format(1.0 / 128.0), "0.007813");
    EXPECT_EQ(format(-1.0 / 128.0), "-0.007813");
    EXPECT_EQ(format(3.0 / 128.0), "0.023438");
    EXPECT_EQ(format(-3.0 / 128.0), "-0.023438");
}

// The double nearest 0.0039765 is just under that decimal, so the product lands
// exactly on a half with a negative residual and the digit rounds down. Summing
// the offset and the residual would round back onto the half and carry up.
TEST(FloatFormatTest, AHalfWithANegativeResidualRoundsDown) {
    EXPECT_EQ(format(0.0039765), "0.003976");
    EXPECT_EQ(format(-0.0039765), "-0.003976");
}

// Each literal is a double just under a decimal half, so it must round down;
// the scaled product rounds up to the half unless the residual is carried in.
// One per decade from 1e5 up, where the error appears (FR-5).
TEST(FloatFormatTest, TheLastPlaceIsCorrectlyRoundedAcrossTheFixedRange) {
    EXPECT_EQ(format(294936.4972605), "294936.497260");
    EXPECT_EQ(format(351522.9986155), "351522.998615");
    EXPECT_EQ(format(6231170.5011655), "6231170.501165");
    EXPECT_EQ(format(70377111.6564405), "70377111.656440");
    EXPECT_EQ(format(319925668.4765525), "319925668.476552");
    EXPECT_EQ(format(-319925668.4765525), "-319925668.476552");
}

// The carry past FLOAT_FIXED_MAX gives the widest result.
TEST(FloatFormatTest, ACarryRunsPastTheFixedMaximum) {
    EXPECT_EQ(format(0.9999999), "1.000000");
    EXPECT_EQ(format(999999999.9999999), "1000000000.000000");
    EXPECT_EQ(format(-999999999.9999999), "-1000000000.000000");
}

// Walks the fixed range against std::to_chars, which is correctly rounded but
// rounds half to even, so exact ties are excluded: a tie has an odd product
// with 128, and that product is exact. The stride is deterministic, so a
// failure names a value to paste into a case (FR-5).
TEST(FloatFormatTest, MatchesACorrectlyRoundedReferenceAcrossTheFixedRange) {
    constexpr int LOWEST_DECADE = -4;
    constexpr int HIGHEST_DECADE = 9;
    constexpr int SAMPLES_PER_DECADE = 20000;
    // An irrational offset within each step keeps samples off the round
    // decimals a stride alone gives, which are the easy cases.
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

// The stride above never lands near a rounding half from 1e5 up. This sweep
// stands on (floor(x * 1e6) + 0.5) / 1e6 for a sample x of each decade and on
// the doubles either side, where a dropped or mis-signed residual changes the
// last digit (FR-5).
TEST(FloatFormatTest, MatchesTheReferenceBesideEveryRoundingHalf) {
    constexpr int LOWEST_DECADE = -4;
    constexpr int HIGHEST_DECADE = 9;
    // Dense enough to reach the rare value where only the residual's sign
    // decides: this density fails an offset-plus-residual comparison, a tenth
    // of it does not.
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

// A value below FLOAT_FIXED_MIN takes exponent notation and is not flattened
// to zeros.
TEST(FloatFormatTest, SmallValuesKeepTheirMagnitudeRatherThanFlattening) {
    EXPECT_EQ(format(0.0000005), "5.000000e-07");
    EXPECT_EQ(format(0.0000004), "4.000000e-07");
}

// Boundary triple at the small end.
TEST(FloatFormatTest, SwitchesToExponentBelowTheFixedMinimum) {
    EXPECT_EQ(format(pico_logging::FLOAT_FIXED_MIN), "0.000100");
    EXPECT_EQ(format(0.000100001), "0.000100");
    // Just under the threshold.
    EXPECT_EQ(format(9.999e-5), "9.999000e-05");
    EXPECT_EQ(format(1e-9), "1.000000e-09");
    EXPECT_EQ(format(-1e-9), "-1.000000e-09");
}

// Boundary triple at the large end.
TEST(FloatFormatTest, SwitchesToExponentAtTheFixedMaximum) {
    EXPECT_EQ(format(pico_logging::FLOAT_FIXED_MAX - 1.0), "999999999.000000");
    EXPECT_EQ(format(pico_logging::FLOAT_FIXED_MAX), "1.000000e+09");
    EXPECT_EQ(format(1e15), "1.000000e+15");
    EXPECT_EQ(format(-1e15), "-1.000000e+15");
}

// A carry from 9.9999995 to 10 would print two integer digits.
TEST(FloatFormatTest, ARoundingCarryRenormalisesTheExponent) {
    EXPECT_EQ(format(9.9999999e-9), "1.000000e-08");
    EXPECT_EQ(format(-9.9999999e-9), "-1.000000e-08");
    EXPECT_EQ(format(9.9999999e20), "1.000000e+21");
}

// A three-digit exponent widens the field and is not truncated.
TEST(FloatFormatTest, TheExtremesOfTheTypeRender) {
    EXPECT_EQ(format(std::numeric_limits<double>::max()), "1.797693e+308");
    EXPECT_EQ(format(std::numeric_limits<double>::min()), "2.225074e-308");
    // Subnormals need hundreds of scaling steps; the loop is bounded.
    EXPECT_EQ(format(std::numeric_limits<double>::denorm_min()),
              "4.940656e-324");
}

// Every result fits the log path's scratch.
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

// A buffer below the worst case yields an empty view and not a prefix (FR-5).
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

// FLOAT_FORMAT_MAX is the worst case over every double: a carry past
// FLOAT_FIXED_MAX with a sign, and it fits exactly.
TEST(FloatFormatTest, TheWorstCaseFitsTheDocumentedMinimum) {
    std::array<char, pico_logging::FLOAT_FORMAT_MAX> exact{};
    const std::string_view widest =
        pico_logging::detail::format_float(-999999999.9999999, exact);

    EXPECT_EQ(widest, "-1000000000.000000");
    EXPECT_EQ(widest.size(), pico_logging::FLOAT_FORMAT_MAX);
}

// A float widens to double and reaches the same formatter; read back through a
// sink, so a LogLine branch that rendered anything else fails here.
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
