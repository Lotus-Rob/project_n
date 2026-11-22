#pragma once

/// @file
/// Measuring points against a road's curve (core::RoadCurves()), for the tests of where walkers are drawn.

#include "core/types.hpp"
#include "core/vec2.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

namespace test {

/// How far `point` lies from the polyline `curve`.
[[nodiscard]] inline f32 OffCurve(core::Vec2 point, std::span<const core::Vec2> curve) {
    f32 nearest = std::numeric_limits<f32>::max();
    for (std::size_t index = 1; index < curve.size(); ++index) {
        const core::Vec2 along = curve[index] - curve[index - 1];
        const core::Vec2 apart = point - curve[index - 1];
        const f32 lengthSquared = (along.x * along.x) + (along.y * along.y);
        const f32 share = std::clamp(((apart.x * along.x) + (apart.y * along.y)) / lengthSquared, 0.0F, 1.0F);
        const core::Vec2 off = point - (curve[index - 1] + (along * share));
        nearest = std::min(nearest, std::hypot(off.x, off.y));
    }
    return nearest;
}

} // namespace test
