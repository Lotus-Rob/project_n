#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/math.hpp"

#include <doctest/doctest.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace {

// What a library might do to the thread: flush subnormal numbers to zero.
void TurnFlushToZeroOn() {
#ifdef __aarch64__
    constexpr u64 FLUSH_BIT = u64{ 1 } << 24U; // FPCR's flush-to-zero
    u64 fpcr = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= FLUSH_BIT;
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__x86_64__) || defined(_M_X64)
    constexpr u32 FLUSH_BITS = 0x8040; // MXCSR's flush-to-zero and denormals-are-zero
    _mm_setcsr(_mm_getcsr() | FLUSH_BITS);
#endif
}

constexpr i32 SAMPLES = 20'001;

// The largest difference from the platform's function over [low, high], relative to max(1, |expected|).
template<typename Ours, typename Platform>
f64 WorstError(Ours ours, Platform platform, f64 low, f64 high) {
    f64 worst = 0.0;
    for (i32 i = 0; i < SAMPLES; ++i) {
        const f64 value = low + ((high - low) * i / (SAMPLES - 1));
        const f64 expected = platform(value);
        worst = std::max(worst, std::abs(ours(value) - expected) / std::max(1.0, std::abs(expected)));
    }
    return worst;
}

constexpr f64 ULPS = 4 * std::numeric_limits<f64>::epsilon();
constexpr f64 ANGLE_RANGE = 1'000.0;

TEST_CASE("Sin and Cos match the platform's to a few ulps") {
    CHECK(
        WorstError(
            [](f64 value) { return core::math::Sin(value); },
            [](f64 value) { return std::sin(value); },
            -ANGLE_RANGE,
            ANGLE_RANGE
        ) < ULPS
    );
    CHECK(
        WorstError(
            [](f64 value) { return core::math::Cos(value); },
            [](f64 value) { return std::cos(value); },
            -ANGLE_RANGE,
            ANGLE_RANGE
        ) < ULPS
    );
}

TEST_CASE("Exp matches the platform's to a few ulps") {
    constexpr f64 RANGE = 700.0;
    CHECK(
        WorstError([](f64 value) { return core::math::Exp(value); }, [](f64 value) { return std::exp(value); }, -RANGE, RANGE) <
        ULPS
    );
}

TEST_CASE("Log matches the platform's to a few ulps") {
    constexpr f64 SMALL = 1e-300;
    constexpr f64 LARGE = 1e6;
    constexpr f64 TWO = 2.0;
    CHECK(
        WorstError([](f64 value) { return core::math::Log(value); }, [](f64 value) { return std::log(value); }, SMALL, LARGE) <
        ULPS
    );
    CHECK(
        WorstError([](f64 value) { return core::math::Log(value); }, [](f64 value) { return std::log(value); }, SMALL, TWO) <
        ULPS
    );
}

TEST_CASE("Pow matches the platform's within its documented error") {
    constexpr f64 BASE = 1.7;
    constexpr f64 RANGE = 100.0;
    constexpr f64 TOLERANCE = 1e-13;
    CHECK(
        WorstError(
            [](f64 exponent) { return core::math::Pow(BASE, exponent); },
            [](f64 exponent) { return std::pow(BASE, exponent); },
            -RANGE,
            RANGE
        ) < TOLERANCE
    );
}

TEST_CASE("Atan2 matches the platform's to a few ulps in every quadrant") {
    constexpr f64 RADIUS = 3.0;
    CHECK(
        WorstError(
            [](f64 angle) { return core::math::Atan2(RADIUS * std::sin(angle), RADIUS * std::cos(angle)); },
            [](f64 angle) { return std::atan2(RADIUS * std::sin(angle), RADIUS * std::cos(angle)); },
            -std::numbers::pi,
            std::numbers::pi
        ) < ULPS
    );
}

constexpr f64 INF = std::numeric_limits<f64>::infinity();

TEST_CASE("Exp overflows to infinity") {
    constexpr f64 TOO_LARGE = 710.0;
    CHECK(core::math::Exp(TOO_LARGE) == INF);
}

TEST_CASE("Exp underflows to 0") {
    constexpr f64 TOO_SMALL = -746.0;
    CHECK(core::math::Exp(TOO_SMALL) == 0.0);
}

TEST_CASE("Exp of 0 is exactly 1") {
    CHECK(core::math::Exp(0.0) == 1.0);
}

TEST_CASE("Log of 1 is exactly 0") {
    CHECK(core::math::Log(1.0) == 0.0);
}

TEST_CASE("Log of 0 is minus infinity") {
    CHECK(core::math::Log(0.0) == -INF);
}

TEST_CASE("Log below 0 is NaN") {
    CHECK(std::isnan(core::math::Log(-1.0)));
}

TEST_CASE("Pow of a negative base needs an integer exponent") {
    constexpr f64 MINUS_TWO = -2.0;
    constexpr f64 MINUS_EIGHT = -8.0;
    constexpr f64 THREE = 3.0;
    constexpr f64 HALF = 0.5;
    CHECK(std::abs((core::math::Pow(MINUS_TWO, THREE) - MINUS_EIGHT) / MINUS_EIGHT) < ULPS);
    CHECK(std::isnan(core::math::Pow(MINUS_TWO, HALF)));
}

TEST_CASE("Pow of a zero base is 0 for a positive exponent") {
    constexpr f64 THREE = 3.0;
    CHECK(core::math::Pow(0.0, THREE) == 0.0);
}

TEST_CASE("Atan2 is 0 at the origin") {
    CHECK(core::math::Atan2(0.0, 0.0) == 0.0);
}

TEST_CASE("Atan2 on the positive y axis is exactly π/2") {
    CHECK(core::math::Atan2(1.0, 0.0) == std::numbers::pi / 2);
}

TEST_CASE("Atan2 on the negative x axis is exactly π") {
    CHECK(core::math::Atan2(0.0, -1.0) == std::numbers::pi);
}

TEST_CASE("Sin of infinity is NaN") {
    CHECK(std::isnan(core::math::Sin(INF)));
}

TEST_CASE("Sin and Cos beyond their range are NaN") {
    constexpr f64 HUGE_ANGLE = 1e20;
    CHECK(std::isnan(core::math::Sin(HUGE_ANGLE)));
    CHECK(std::isnan(core::math::Cos(-HUGE_ANGLE)));
}

TEST_CASE("the f32 overloads round the f64 result once") {
    constexpr f32 ANGLE = 0.7F;
    CHECK(core::math::Sin(ANGLE) == static_cast<f32>(core::math::Sin(static_cast<f64>(ANGLE))));
}

TEST_CASE("setting the floating-point environment turns flush-to-zero off") {
    TurnFlushToZeroOn();
    CHECK_FALSE(core::math::FloatingPointEnvironmentIsSet());
    core::math::SetFloatingPointEnvironment();
    CHECK(core::math::FloatingPointEnvironmentIsSet());
}

TEST_CASE("setting the floating-point environment restores rounding to nearest") {
    std::fesetround(FE_UPWARD);
    CHECK_FALSE(core::math::FloatingPointEnvironmentIsSet());
    core::math::SetFloatingPointEnvironment();
    CHECK(core::math::FloatingPointEnvironmentIsSet());
}

TEST_CASE("subnormal results survive once the environment is set") {
    TurnFlushToZeroOn();
    core::math::SetFloatingPointEnvironment();
    const volatile f64 smallest = std::numeric_limits<f64>::min(); // volatile: divided at run time
    const f64 subnormal = smallest / 2.0;
    CHECK(subnormal > 0.0);
}

TEST_CASE("Atan2 with both coordinates infinite is a diagonal as in std::atan2") {
    constexpr f64 QUARTER_PI = std::numbers::pi / 4;
    CHECK(core::math::Atan2(INF, INF) == doctest::Approx(QUARTER_PI));
    CHECK(core::math::Atan2(INF, -INF) == doctest::Approx(3 * QUARTER_PI));
    CHECK(core::math::Atan2(-INF, INF) == doctest::Approx(-QUARTER_PI));
    CHECK(core::math::Atan2(-INF, -INF) == doctest::Approx(-3 * QUARTER_PI));
}

/// One input of a pinned function, named for why it is there; `second` is Pow's exponent and Atan2's x.
struct Sample {
    constexpr Sample(std::string_view label, f64 firstInput, f64 secondInput = 0.0)
      : name(label)
      , first(firstInput)
      , second(secondInput) {
    }

    std::string_view name;
    f64 first;
    f64 second;
};

constexpr f64 HALF_TURN = std::numbers::pi;
constexpr f64 NOT_A_NUMBER = std::numeric_limits<f64>::quiet_NaN();
constexpr f64 SMALLEST_SUBNORMAL = std::numeric_limits<f64>::denorm_min();
constexpr f64 SMALLEST_NORMAL = std::numeric_limits<f64>::min();
constexpr f64 LARGEST = std::numeric_limits<f64>::max();
constexpr f64 QUARTER_TURN_LIMIT = 0x1.0p20 * (HALF_TURN / 2); // the largest angle Sin and Cos reduce exactly
constexpr f64 EXP_LARGEST = 7.09782712893383973096e+02;

constexpr std::size_t PINNED_SAMPLES = 20;

constexpr std::array<Sample, PINNED_SAMPLES> ANGLES{ {
    { "zero", 0.0 },
    { "negative zero", -0.0 },
    { "smallest subnormal", SMALLEST_SUBNORMAL },
    { "tiny", 1e-10 },
    { "inside the first quarter turn", 0.5 },
    { "the first reduction boundary, pi/4", HALF_TURN / 4 },
    { "just past the first reduction boundary", (HALF_TURN / 4) + 1e-9 },
    { "one radian", 1.0 },
    { "a quarter turn", HALF_TURN / 2 },
    { "a half turn", HALF_TURN },
    { "three quarter turns", 3 * (HALF_TURN / 2) },
    { "a full turn", 2 * HALF_TURN },
    { "negative one radian", -1.0 },
    { "negative ten", -10.0 },
    { "one hundred", 100.0 },
    { "the end of the matched range", 1000.0 },
    { "a large angle", 1e5 },
    { "the largest accepted angle", QUARTER_TURN_LIMIT },
    { "past the limit, NaN", QUARTER_TURN_LIMIT * 1.0001 },
    { "infinity, NaN", std::numeric_limits<f64>::infinity() },
} };

constexpr std::array<Sample, PINNED_SAMPLES> POWERS{ {
    { "zero", 0.0 },
    { "negative zero", -0.0 },
    { "smallest subnormal", SMALLEST_SUBNORMAL },
    { "tiny", 1e-10 },
    { "the reduction boundary, ln(2)/2", std::numbers::ln2 / 2 },
    { "just past the reduction boundary", 0.35 },
    { "one half", 0.5 },
    { "one", 1.0 },
    { "negative one", -1.0 },
    { "ten", 10.0 },
    { "negative ten", -10.0 },
    { "one hundred", 100.0 },
    { "five hundred", 500.0 },
    { "the end of the matched range", 700.0 },
    { "the largest accepted power", EXP_LARGEST },
    { "just past it, infinity", 710.0 },
    { "negative seven hundred", -700.0 },
    { "a subnormal result", -745.0 },
    { "past the smallest, zero", -746.0 },
    { "not a number", NOT_A_NUMBER },
} };

constexpr std::array<Sample, PINNED_SAMPLES> LOGARITHMS{ {
    { "one", 1.0 },
    { "zero, minus infinity", 0.0 },
    { "negative zero, minus infinity", -0.0 },
    { "smallest subnormal", SMALLEST_SUBNORMAL },
    { "smallest normal", SMALLEST_NORMAL },
    { "the start of the matched range", 1e-300 },
    { "one half", 0.5 },
    { "the mantissa boundary, sqrt(2)/2", std::numbers::sqrt2 / 2 },
    { "the other mantissa boundary, sqrt(2)", std::numbers::sqrt2 },
    { "just under sqrt(2)", std::bit_cast<f64>(std::bit_cast<u64>(std::numbers::sqrt2) - 1) },
    { "one and a half", 1.5 },
    { "two", 2.0 },
    { "e", std::numbers::e },
    { "ten", 10.0 },
    { "the end of the matched range", 1e6 },
    { "a huge value", 1e300 },
    { "the largest finite", LARGEST },
    { "negative, NaN", -1.0 },
    { "infinity", std::numeric_limits<f64>::infinity() },
    { "not a number", NOT_A_NUMBER },
} };

constexpr std::array<Sample, PINNED_SAMPLES> BASES_AND_EXPONENTS{ {
    { "exponent zero", 5.0 },
    { "base one", 1.0, 7.0 },
    { "exponent one", 1.7, 1.0 },
    { "the matched base at the top of its range", 1.7, 100.0 },
    { "the matched base at the bottom of its range", 1.7, -100.0 },
    { "an integer power of two", 2.0, 10.0 },
    { "a square root", 2.0, 0.5 },
    { "a negative power of ten", 10.0, -3.0 },
    { "a fraction cubed", 0.5, 3.0 },
    { "e to the one", std::numbers::e, 1.0 },
    { "just under overflow", 2.0, 1023.0 },
    { "two to the 1024th, finite: the product stays inside Exp's range", 2.0, 1024.0 },
    { "a subnormal result", 2.0, -1074.0 },
    { "negative base, odd exponent", -2.0, 3.0 },
    { "negative base, even exponent", -2.0, 2.0 },
    { "negative base, fractional exponent, NaN", -2.0, 0.5 },
    { "zero base, positive exponent", 0.0, 3.0 },
    { "zero base, negative exponent, infinity", 0.0, -1.0 },
    { "a huge base", 1e308, 1.5 },
    { "not a number base", NOT_A_NUMBER, 2.0 },
} };

// `first` is y, `second` is x.
constexpr std::array<Sample, PINNED_SAMPLES> POINTS{ {
    { "the origin", 0.0 },
    { "the positive y axis", 1.0 },
    { "the negative x axis", 0.0, -1.0 },
    { "the positive x axis", 0.0, 1.0 },
    { "the negative y axis", -1.0 },
    { "the first diagonal", 1.0, 1.0 },
    { "the ratio boundary, tan(pi/12)", 2 - std::numbers::sqrt3, 1.0 },
    { "just past the ratio boundary", 0.27, 1.0 },
    { "ratio one half", 1.0, 2.0 },
    { "ratio two", 2.0, 1.0 },
    { "second quadrant", 1.0, -1.0 },
    { "third quadrant", -1.0, -1.0 },
    { "fourth quadrant", -1.0, 1.0 },
    { "a tiny ratio", 1e-10, 1.0 },
    { "a huge ratio", 1e10, 1.0 },
    { "a subnormal y", SMALLEST_SUBNORMAL, 1.0 },
    { "both infinite", std::numeric_limits<f64>::infinity(), std::numeric_limits<f64>::infinity() },
    { "infinite y, negative infinite x", std::numeric_limits<f64>::infinity(), -std::numeric_limits<f64>::infinity() },
    { "finite y, negative infinite x", 1.0, -std::numeric_limits<f64>::infinity() },
    { "not a number", NOT_A_NUMBER, 1.0 },
} };

struct Pinned {
    std::string_view function;
    f64 (*apply)(f64, f64);
    const std::array<Sample, PINNED_SAMPLES>* samples;
};

constexpr std::size_t PINNED_FUNCTIONS = 6;

const std::array<Pinned, PINNED_FUNCTIONS>& PinnedFunctions() {
    static const std::array<Pinned, PINNED_FUNCTIONS> pinned{ {
        { .function = "Sin", .apply = [](f64 value, f64) { return core::math::Sin(value); }, .samples = &ANGLES },
        { .function = "Cos", .apply = [](f64 value, f64) { return core::math::Cos(value); }, .samples = &ANGLES },
        { .function = "Exp", .apply = [](f64 value, f64) { return core::math::Exp(value); }, .samples = &POWERS },
        { .function = "Log", .apply = [](f64 value, f64) { return core::math::Log(value); }, .samples = &LOGARITHMS },
        { .function = "Pow",
          .apply = [](f64 base, f64 exponent) { return core::math::Pow(base, exponent); },
          .samples = &BASES_AND_EXPONENTS },
        { .function = "Atan2",
          .apply = [](f64 pointY, f64 pointX) { return core::math::Atan2(pointY, pointX); },
          .samples = &POINTS },
    } };
    return pinned;
}

using Row = std::array<u64, PINNED_SAMPLES>;
using Table = std::array<Row, PINNED_FUNCTIONS>;

// One table per toolchain whose libm and code generation may round differently; keys that agree bit for bit share one.
#ifdef _MSC_VER
constexpr std::string_view TOOLCHAIN = "msvc";
#elif defined(__clang__) && defined(__aarch64__)
constexpr std::string_view TOOLCHAIN = "clang-arm64";
#elifdef __clang__
constexpr std::string_view TOOLCHAIN = "clang-x64";
#else
constexpr std::string_view TOOLCHAIN = "gcc-x64";
#endif

// msvc, clang-x64 and gcc-x64 agree on every bit, so they share the table below; clang-arm64 has none yet.
#if defined(__clang__) && defined(__aarch64__)
constexpr bool HAS_PINNED_TABLE = false;
constexpr Table PINNED{};
#else
constexpr bool HAS_PINNED_TABLE = true;
constexpr Table PINNED{ {
    {
        // Sin
        0x0000000000000000U, // zero
        0x0000000000000000U, // negative zero
        0x0000000000000001U, // smallest subnormal
        0x3ddb7cdfd9d7bdbbU, // tiny
        0x3fdeaee8744b05f0U, // inside the first quarter turn
        0x3fe6a09e667f3bccU, // the first reduction boundary, pi/4
        0x3fe6a09e66e06ae8U, // just past the first reduction boundary
        0x3feaed548f090ceeU, // one radian
        0x3ff0000000000000U, // a quarter turn
        0x3ca1a62633145c07U, // a half turn
        0xbff0000000000000U, // three quarter turns
        0xbcb1a62633145c07U, // a full turn
        0xbfeaed548f090ceeU, // negative one radian
        0x3fe1689ef5f34f52U, // negative ten
        0xbfe03425b78c4db8U, // one hundred
        0x3fea75cc150a206cU, // the end of the matched range
        0x3fa24daa9c527e96U, // a large angle
        0xbdd1a62633145c07U, // the largest accepted angle
        0x7ff8000000000000U, // past the limit, NaN
        0x7ff8000000000000U, // infinity, NaN
    },
    {
        // Cos
        0x3ff0000000000000U, // zero
        0x3ff0000000000000U, // negative zero
        0x3ff0000000000000U, // smallest subnormal
        0x3ff0000000000000U, // tiny
        0x3fec1528065b7d50U, // inside the first quarter turn
        0x3fe6a09e667f3bcdU, // the first reduction boundary, pi/4
        0x3fe6a09e661e0cb2U, // just past the first reduction boundary
        0x3fe14a280fb5068cU, // one radian
        0x3c91a62633145c07U, // a quarter turn
        0xbff0000000000000U, // a half turn
        0xbcaa79394c9e8a0aU, // three quarter turns
        0x3ff0000000000000U, // a full turn
        0x3fe14a280fb5068cU, // negative one radian
        0xbfead9ac890c6b1fU, // negative ten
        0x3feb981dbf665fe0U, // one hundred
        0x3fe1ff026793f1bbU, // the end of the matched range
        0xbfeffac3841b3da7U, // a large angle
        0x3ff0000000000000U, // the largest accepted angle
        0x7ff8000000000000U, // past the limit, NaN
        0x7ff8000000000000U, // infinity, NaN
    },
    {
        // Exp
        0x3ff0000000000000U, // zero
        0x3ff0000000000000U, // negative zero
        0x3ff0000000000000U, // smallest subnormal
        0x3ff000000006df38U, // tiny
        0x3ff6a09e667f3bccU, // the reduction boundary, ln(2)/2
        0x3ff6b4802c805decU, // just past the reduction boundary
        0x3ffa61298e1e069cU, // one half
        0x4005bf0a8b14576aU, // one
        0x3fd78b56362cef38U, // negative one
        0x40d5829dcf950560U, // ten
        0x3f07cd79b5647c9aU, // negative ten
        0x48f3494a9b171bf5U, // one hundred
        0x6d045ba2a9f7e438U, // five hundred
        0x7f0d945df4f8ec8eU, // the end of the matched range
        0x7fefffffffffff2aU, // the largest accepted power
        0x7ff0000000000000U, // just past it, infinity
        0x00d14f2b0fb9307fU, // negative seven hundred
        0x0000000000000001U, // a subnormal result
        0x0000000000000000U, // past the smallest, zero
        0x7ff8000000000000U, // not a number
    },
    {
        // Log
        0x0000000000000000U, // one
        0xfff0000000000000U, // zero, minus infinity
        0xfff0000000000000U, // negative zero, minus infinity
        0xc0874385446d71c3U, // smallest subnormal
        0xc086232bdd7abcd2U, // smallest normal
        0xc085963447f87fb5U, // the start of the matched range
        0xbfe62e42fefa39efU, // one half
        0xbfd62e42fefa39eeU, // the mantissa boundary, sqrt(2)/2
        0x3fd62e42fefa39f0U, // the other mantissa boundary, sqrt(2)
        0x3fd62e42fefa39edU, // just under sqrt(2)
        0x3fd9f323ecbf984cU, // one and a half
        0x3fe62e42fefa39efU, // two
        0x3ff0000000000000U, // e
        0x40026bb1bbb55516U, // ten
        0x402ba18a998fffa0U, // the end of the matched range
        0x4085963447f87fb5U, // a huge value
        0x40862e42fefa39efU, // the largest finite
        0x7ff8000000000000U, // negative, NaN
        0x7ff0000000000000U, // infinity
        0x7ff8000000000000U, // not a number
    },
    {
        // Pow
        0x3ff0000000000000U, // exponent zero
        0x3ff0000000000000U, // base one
        0x3ffb333333333333U, // exponent one
        0x44b77b5b0502583eU, // the matched base at the top of its range
        0x3b25cdd75856ade0U, // the matched base at the bottom of its range
        0x4090000000000000U, // an integer power of two
        0x3ff6a09e667f3bccU, // a square root
        0x3f50624dd2f1a9f9U, // a negative power of ten
        0x3fc0000000000001U, // a fraction cubed
        0x4005bf0a8b14576aU, // e to the one
        0x7fe000000000008dU, // just under overflow
        0x7fefffffffffff2aU, // two to the 1024th, finite: the product stays inside Exp's range
        0x0000000000000001U, // a subnormal result
        0xc01ffffffffffffeU, // negative base, odd exponent
        0x4010000000000000U, // negative base, even exponent
        0x7ff8000000000000U, // negative base, fractional exponent, NaN
        0x0000000000000000U, // zero base, positive exponent
        0x7ff0000000000000U, // zero base, negative exponent, infinity
        0x7ff0000000000000U, // a huge base
        0x7ff8000000000000U, // not a number base
    },
    {
        // Atan2
        0x0000000000000000U, // the origin
        0x3ff921fb54442d18U, // the positive y axis
        0x400921fb54442d18U, // the negative x axis
        0x0000000000000000U, // the positive x axis
        0xbff921fb54442d18U, // the negative y axis
        0x3fe921fb54442d18U, // the first diagonal
        0x3fd0c152382d7367U, // the ratio boundary, tan(pi/12)
        0x3fd0e0a79a2559adU, // just past the ratio boundary
        0x3fddac670561bb4eU, // ratio one half
        0x3ff1b6e192ebbe44U, // ratio two
        0x4002d97c7f3321d2U, // second quadrant
        0xc002d97c7f3321d2U, // third quadrant
        0xbfe921fb54442d18U, // fourth quadrant
        0x3ddb7cdfd9d7bdbbU, // a tiny ratio
        0x3ff921fb543d4de0U, // a huge ratio
        0x0000000000000001U, // a subnormal y
        0x3fe921fb54442d18U, // both infinite
        0x4002d97c7f3321d2U, // infinite y, negative infinite x
        0x400921fb54442d18U, // finite y, negative infinite x
        0x7ff8000000000000U, // not a number
    },
} };
#endif

Table Computed() {
    Table table{};
    const auto& functions = PinnedFunctions();
    for (std::size_t row = 0; row < PINNED_FUNCTIONS; ++row) {
        for (std::size_t col = 0; col < PINNED_SAMPLES; ++col) {
            const Sample& sample = (*functions[row].samples)[col];
            table[row][col] = std::bit_cast<u64>(functions[row].apply(sample.first, sample.second));
        }
    }
    return table;
}

/// The computed table as the C++ source of `PINNED`, for pasting into this file.
std::string TableSource(const Table& table) {
    std::string source = "constexpr Table PINNED{ {\n";
    for (std::size_t row = 0; row < PINNED_FUNCTIONS; ++row) {
        source += std::format("    {{ // {}\n", PinnedFunctions()[row].function);
        for (std::size_t col = 0; col < PINNED_SAMPLES; ++col) {
            source += std::format("        0x{:016x}U, // {}\n", table[row][col], (*PinnedFunctions()[row].samples)[col].name);
        }
        source += "    },\n";
    }
    source += "} };\n";
    return source;
}

// Not part of a run: `--test-case='print the pinned core::math table' --no-skip` prints the table of this toolchain.
TEST_CASE("print the pinned core::math table" * doctest::skip()) {
    MESSAGE(TOOLCHAIN, "\n", TableSource(Computed()));
}

TEST_CASE("core::math gives its pinned bits on this toolchain") {
    const Table computed = Computed();
    if (!HAS_PINNED_TABLE) {
        FAIL("no pinned table for ", TOOLCHAIN, "; add this one:\n", TableSource(computed));
    }
    for (std::size_t row = 0; row < PINNED_FUNCTIONS; ++row) {
        for (std::size_t col = 0; col < PINNED_SAMPLES; ++col) {
            CAPTURE(PinnedFunctions()[row].function);
            CAPTURE((*PinnedFunctions()[row].samples)[col].name);
            CHECK(computed[row][col] == PINNED[row][col]);
        }
    }
}

} // namespace
