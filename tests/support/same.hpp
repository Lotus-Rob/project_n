#pragma once

/// @file
/// Bit-for-bit comparisons of a determinism test's values: points and events.

#include "core/frame.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include <algorithm>
#include <bit>
#include <vector>

namespace test {

/// Whether `lhs` and `rhs` have the same bits, as determinism means.
[[nodiscard]] inline bool SamePoint(core::Vec2 lhs, core::Vec2 rhs) {
    return std::bit_cast<u32>(lhs.x) == std::bit_cast<u32>(rhs.x) && std::bit_cast<u32>(lhs.y) == std::bit_cast<u32>(rhs.y);
}

/// Whether two runs' events are the same, field by field, the tick each fell on included.
[[nodiscard]] inline bool SameEvents(const std::vector<core::Event>& lhs, const std::vector<core::Event>& rhs) {
    return std::ranges::equal(lhs, rhs, [](const core::Event& one, const core::Event& two) {
        return one.tick == two.tick && one.kind == two.kind && one.hit == two.hit && one.use == two.use &&
               one.target == two.target && one.value == two.value &&
               std::bit_cast<u64>(one.position) == std::bit_cast<u64>(two.position);
    });
}

/// Whether two runs' events are the same, field by field, except the tick each fell on.
[[nodiscard]] inline bool SameEventsIgnoringTick(const std::vector<core::Event>& lhs, const std::vector<core::Event>& rhs) {
    return std::ranges::equal(lhs, rhs, [](const core::Event& one, const core::Event& two) {
        return one.kind == two.kind && one.hit == two.hit && one.use == two.use && one.target == two.target &&
               one.value == two.value && std::bit_cast<u64>(one.position) == std::bit_cast<u64>(two.position);
    });
}

} // namespace test
