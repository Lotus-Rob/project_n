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

} // namespace core
