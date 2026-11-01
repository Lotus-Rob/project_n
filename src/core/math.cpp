#include "core/math.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace core::math {

namespace {

// Polynomials are Taylor series, truncated where the next term falls below 2^-53 of the result
// over the reduced range. Coefficients are exact reciprocals rounded once, at compile time.
template<std::size_t N>
constexpr std::array<f64, N> ReciprocalFactorials(u32 first, u32 step) {
    std::array<f64, N> coefficients{};
    for (std::size_t i = 0; i < N; ++i) {
        f64 factorial = 1.0;
        for (u32 factor = 2; factor <= first + (step * static_cast<u32>(i)); ++factor) {
            factorial *= static_cast<f64>(factor);
        }
        coefficients[i] = 1.0 / factorial;
    }
    return coefficients;
}

// sin(r) = r(1 - r²/3! + r⁴/5! - ...), cos(r) = 1 - r²/2! + r⁴/4! - ..., for |r| <= π/4.
constexpr auto SIN = ReciprocalFactorials<9>(1, 2);
constexpr auto COS = ReciprocalFactorials<10>(0, 2);
// e^r = 1 + r + r²/2! + ..., for |r| <= ln(2)/2.
constexpr auto EXP = ReciprocalFactorials<15>(0, 1);

// Horner's rule in `power`, with alternating signs when `alternate`.
template<std::size_t N>
f64 Horner(const std::array<f64, N>& coefficients, f64 power, bool alternate) {
    f64 sum = 0.0;
    for (std::size_t i = N; i-- > 0;) {
        sum = (sum * power) + ((alternate && i % 2 == 1) ? -coefficients[i] : coefficients[i]);
    }
    return sum;
}

// 1 / (2n + 1) for the odd-power series of atan and atanh.
f64 OddReciprocal(i32 term) {
    return 1.0 / static_cast<f64>((2 * term) + 1);
}

// π/2 in three parts (Cody and Waite): multiples of the first two are exact below 2^20.
constexpr f64 PIO2_1 = 1.57079632673412561417e+00;
constexpr f64 PIO2_2 = 6.07710050630396597660e-11;
constexpr f64 PIO2_3 = 2.02226624879595063154e-21;
constexpr f64 TWO_OVER_PI = 2 / std::numbers::pi;
// The reduction is exact only below 2^20 quarter turns; beyond, Sin and Cos are NaN.
constexpr f64 SIN_COS_LIMIT = 0x1.0p20 * (std::numbers::pi / 2);
// ln(2) in two parts: multiples of the first are exact below 2^11.
constexpr f64 LN2_HI = 6.93147180369123816490e-01; // NOLINT(modernize-use-std-numbers): ln(2)'s leading bits only
constexpr f64 LN2_LO = 1.90821492927058770002e-10;
constexpr f64 TAN_PI_12 = 2 - std::numbers::sqrt3;
constexpr f64 PI_OVER_6 = std::numbers::pi / 6;
constexpr f64 EXP_MAX = 7.09782712893383973096e+02;
constexpr f64 EXP_MIN = -7.45133219101941108420e+02;

// sin for quadrant offset 0, cos for 1: angle = n·π/2 + r, and the quadrant picks the series.
f64 SinCos(f64 angle, i64 offset) {
    if (!(std::abs(angle) <= SIN_COS_LIMIT)) { // NaN fails too
        return std::numeric_limits<f64>::quiet_NaN();
    }
    const f64 quarters = std::round(angle * TWO_OVER_PI);
    const f64 reduced = ((angle - (quarters * PIO2_1)) - (quarters * PIO2_2)) - (quarters * PIO2_3);
    const f64 squared = reduced * reduced;
    const i64 quadrant = (static_cast<i64>(quarters) + offset) & 3;
    const f64 value = (quadrant % 2 == 0) ? reduced * Horner(SIN, squared, true) : Horner(COS, squared, true);
    return quadrant >= 2 ? -value : value;
}

// atan for 0 <= ratio <= 1: past tan(π/12), atan(t) = π/6 + atan((t√3 - 1) / (t + √3)) brings the
// argument within ±tan(π/12), where atan(u) = u - u³/3 + u⁵/5 - ...
f64 AtanUnit(f64 ratio) {
    constexpr i32 TERMS = 15;
    f64 offset = 0.0;
    if (ratio > TAN_PI_12) {
        ratio = ((ratio * std::numbers::sqrt3) - 1.0) / (ratio + std::numbers::sqrt3);
        offset = PI_OVER_6;
    }
    const f64 squared = ratio * ratio;
    f64 sum = 0.0;
    for (i32 term = TERMS - 1; term >= 0; --term) {
        sum = (sum * squared) + (term % 2 == 1 ? -OddReciprocal(term) : OddReciprocal(term));
    }
    return offset + (ratio * sum);
}

} // namespace

f64 Sin(f64 radians) {
    return SinCos(radians, 0);
}

f64 Cos(f64 radians) {
    return SinCos(radians, 1);
}

f64 Exp(f64 power) {
    if (std::isnan(power)) {
        return power;
    }
    if (power > EXP_MAX) {
        return std::numeric_limits<f64>::infinity();
    }
    if (power < EXP_MIN) {
        return 0.0;
    }
    // e^x = 2^n · e^r with x = n·ln(2) + r.
    const f64 halvings = std::round(power * std::numbers::log2e);
    const f64 reduced = (power - (halvings * LN2_HI)) - (halvings * LN2_LO);
    return std::ldexp(Horner(EXP, reduced, false), static_cast<i32>(halvings));
}

f64 Log(f64 value) {
    if (std::isnan(value) || value < 0.0) {
        return std::numeric_limits<f64>::quiet_NaN();
    }
    if (value == 0.0) {
        return -std::numeric_limits<f64>::infinity();
    }
    if (std::isinf(value)) {
        return value;
    }
    // x = m · 2^e with m in [√½, √2); log(m) = 2·atanh(f) = 2(f + f³/3 + f⁵/5 + ...), f = (m-1)/(m+1).
    constexpr i32 TERMS = 11;
    i32 exponent = 0;
    f64 mantissa = std::frexp(value, &exponent);
    if (mantissa < std::numbers::sqrt2 / 2) {
        mantissa *= 2;
        --exponent;
    }
    const f64 ratio = (mantissa - 1.0) / (mantissa + 1.0);
    const f64 squared = ratio * ratio;
    f64 sum = 0.0;
    for (i32 term = TERMS - 1; term >= 0; --term) {
        sum = (sum * squared) + OddReciprocal(term);
    }
    const auto scale = static_cast<f64>(exponent);
    return (scale * LN2_HI) + ((2 * ratio * sum) + (scale * LN2_LO));
}

f64 Pow(f64 base, f64 exponent) {
    if (exponent == 0.0 || base == 1.0) {
        return 1.0;
    }
    if (std::isnan(base) || std::isnan(exponent)) {
        return std::numeric_limits<f64>::quiet_NaN();
    }
    f64 sign = 1.0;
    if (base < 0.0) {
        if (std::fmod(exponent, 1.0) != 0.0) {
            return std::numeric_limits<f64>::quiet_NaN();
        }
        sign = std::fmod(exponent, 2) == 0.0 ? 1.0 : -1.0;
        base = -base;
    }
    if (base == 0.0) {
        return exponent > 0.0 ? 0.0 : std::numeric_limits<f64>::infinity();
    }
    return sign * Exp(exponent * Log(base));
}

f64 Atan2(f64 pointY, f64 pointX) {
    if (std::isnan(pointX) || std::isnan(pointY)) {
        return std::numeric_limits<f64>::quiet_NaN();
    }
    if (pointX == 0.0 && pointY == 0.0) {
        return 0.0;
    }
    const f64 run = std::abs(pointX);
    const f64 rise = std::abs(pointY);
    const f64 angle = rise <= run ? AtanUnit(rise / run) : (std::numbers::pi / 2) - AtanUnit(run / rise);
    const f64 halfTurn = pointX < 0.0 ? std::numbers::pi - angle : angle;
    return pointY < 0.0 ? -halfTurn : halfTurn;
}

namespace {

#ifdef __aarch64__
// FPCR's flush-to-zero bit.
constexpr u64 FLUSH_BITS = u64{ 1 } << 24U;

u64 Fpcr() {
    u64 fpcr = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    return fpcr;
}

u64 FlushBits() {
    return Fpcr() & FLUSH_BITS;
}

void ClearFlushBits() {
    const u64 fpcr = Fpcr() & ~FLUSH_BITS;
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
}
#elif defined(__x86_64__) || defined(_M_X64)
// MXCSR's flush-to-zero and denormals-are-zero bits.
constexpr u32 FLUSH_BITS = 0x8040;

u32 FlushBits() {
    return _mm_getcsr() & FLUSH_BITS;
}

void ClearFlushBits() {
    _mm_setcsr(_mm_getcsr() & ~FLUSH_BITS);
}
#else
#error "core::math needs the flush-to-zero controls of this CPU"
#endif

} // namespace

void SetFloatingPointEnvironment() {
    std::fesetround(FE_TONEAREST);
    ClearFlushBits();
}

bool FloatingPointEnvironmentIsSet() {
    return std::fegetround() == FE_TONEAREST && FlushBits() == 0;
}

} // namespace core::math
