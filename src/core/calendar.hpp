#pragma once

/// @file
/// @ingroup core
/// The world calendar's units, which core's journeys and the game's world clock share.

#include "core/types.hpp"

namespace core {

/// How many simulation ticks a day of world time lasts (3 s at 60 Hz): the world clock's day (game::TICKS_PER_DAY is
/// this one), and the day a journey's stay counts and drifts by.
inline constexpr u64 TICKS_PER_DAY = 180;

} // namespace core
