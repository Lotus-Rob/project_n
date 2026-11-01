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

} // namespace
