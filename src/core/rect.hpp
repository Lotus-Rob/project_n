#pragma once

/// @file
/// @ingroup core
/// An axis-aligned rectangle of the world: what a view shows, and what the world map is asked about.

#include "core/vec2.hpp"

namespace core {

/// An axis-aligned rectangle of the world, in world units.
struct Rect {
    /// The top-left corner.
    Vec2 min{};
    /// The bottom-right corner.
    Vec2 max{};
};

/// Whether `point` lies within `area`, its edges included; a point with a NaN coordinate never does.
[[nodiscard]] constexpr bool Contains(const Rect& area, Vec2 point) {
    return point.x >= area.min.x && point.x <= area.max.x && point.y >= area.min.y && point.y <= area.max.y;
}

} // namespace core
