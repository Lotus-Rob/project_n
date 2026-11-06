#pragma once

/// @file
/// @ingroup core
/// When a system runs (design doc 0002 §7.3): every k simulation ticks at an offset, and, for a
/// system that time-slices its entities, which of them it processes on a tick. Pure functions of
/// the tick and the stable ID, so the schedule is the same on every machine and in every replay.

#include "core/ecs.hpp"
#include "core/random.hpp"
#include "core/types.hpp"

#include <utility>

namespace core {

/// How often a system runs: on simulation ticks whose number modulo `every` is `offset`.
struct Rate {
    /// Runs once every this many simulation ticks (at least 1; 1 is every tick).
    u32 every = 1;
    /// Which tick of each `every` it runs on, below `every`: spreads systems of one rate over ticks.
    u32 offset = 0;
};

/// Whether a system at `rate` runs on `simulationTick`.
[[nodiscard]] constexpr bool RunsOn(Rate rate, u64 simulationTick) {
    return simulationTick % rate.every == rate.offset;
}

/// Whether a system time-sliced over `every` ticks processes `stableId`'s entity on
/// `simulationTick`: each entity once every `every` ticks, the entities spread evenly over them by
/// a hash of their stable IDs, never by EnTT handle or storage order.
[[nodiscard]] constexpr bool InSlice(u32 every, u64 simulationTick, StableId stableId) {
    return (simulationTick + detail::Mix(std::to_underlying(stableId))) % every == 0;
}

} // namespace core
