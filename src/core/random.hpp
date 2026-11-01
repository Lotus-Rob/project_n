#pragma once

/// @file
/// @ingroup core
/// Counter-based random numbers for the simulation (design doc 0002 §7.4): each number is a hash of
/// its key (seed, state counter, stable ID, purpose, draw index), so it doesn't depend on which
/// thread draws it or in what order. Never use `std::*_distribution` in the simulation: their
/// results differ between standard libraries.

#include "core/types.hpp"

#include <initializer_list>

namespace core {

namespace detail {

/// SplitMix64's finaliser: every input bit of `value` affects every output bit.
[[nodiscard]] constexpr u64 Mix(u64 value) {
    constexpr u64 MULTIPLIER_1 = 0xbf58'476d'1ce4'e5b9ULL;
    constexpr u64 MULTIPLIER_2 = 0x94d0'49bb'1331'11ebULL;
    constexpr u32 SHIFT_1 = 30;
    constexpr u32 SHIFT_2 = 27;
    constexpr u32 SHIFT_3 = 31;
    value = (value ^ (value >> SHIFT_1)) * MULTIPLIER_1;
    value = (value ^ (value >> SHIFT_2)) * MULTIPLIER_2;
    return value ^ (value >> SHIFT_3);
}

} // namespace detail

/// A uniformly distributed 64-bit number, a function of its key alone: the game's `seed`, the
/// `stateCounter` (TickCounters::state), the `stableId` of the entity drawing (0 for none), the
/// `purpose` (a value per kind of use, so two uses never share numbers) and the `draw` index for
/// several numbers in one use. The algorithm is part of the save and replay format.
[[nodiscard]] constexpr u64 Random(u64 seed, u64 stateCounter, u64 stableId, u32 purpose, u32 draw) {
    constexpr u64 GOLDEN = 0x9e37'79b9'7f4a'7c15ULL; // separates the key's parts, so their order matters
    u64 hash = detail::Mix(seed);
    for (const u64 part : { stateCounter, stableId, (u64{ purpose } << 32U) | draw }) {
        hash = detail::Mix(hash ^ detail::Mix(part + GOLDEN));
    }
    return hash;
}

/// A random number's value in [0, 1), uniform, from its top 53 bits.
[[nodiscard]] constexpr f64 ToUnit(u64 random) {
    constexpr f64 TWO_TO_MINUS_53 = 0x1.0p-53;
    constexpr u32 UNUSED_BITS = 64 - 53;
    return static_cast<f64>(random >> UNUSED_BITS) * TWO_TO_MINUS_53;
}

/// A random number's value in [0, `bound`), uniform to within `bound` / 2^32 (Lemire's
/// multiply-shift on its top 32 bits).
[[nodiscard]] constexpr u32 ToBelow(u64 random, u32 bound) {
    constexpr u32 HALF = 32;
    return static_cast<u32>(((random >> HALF) * u64{ bound }) >> HALF);
}

} // namespace core
