#pragma once

/// @file
/// @ingroup core
/// The kinds of hit, which the ability data and the events name.

#include "core/types.hpp"

#include <cstddef>

namespace core {

/// The kind of a hit, which decides how its damage number reads.
enum class HitKind : u8 {
    Damage,  ///< Harm.
    Healing, ///< Health restored.
    Poison,  ///< Harm over time from poison.
    Burn,    ///< Harm over time from fire.
};
/// How many kinds HitKind names.
inline constexpr std::size_t HIT_KINDS = 4;

} // namespace core
