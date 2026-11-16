#pragma once

/// @file
/// @ingroup core
/// The world calendar's units, which core's journeys and the game's world clock share.

#include "core/types.hpp"

namespace core {

/// How many simulation ticks a day of world time lasts (3 s at 60 Hz): the world clock's day (game::TICKS_PER_DAY is
/// this one), and the day a journey's stay counts and drifts by.
inline constexpr u64 TICKS_PER_DAY = 180;
/// How many days a month of world time lasts: the world clock's month (game::DAYS_PER_MONTH is this one), over whose days
/// the visits of a month set out.
inline constexpr u64 DAYS_PER_MONTH = 30;
/// How many simulation ticks a month of world time lasts.
inline constexpr u64 TICKS_PER_MONTH = TICKS_PER_DAY * DAYS_PER_MONTH;

} // namespace core
