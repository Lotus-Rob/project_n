#pragma once

/// @file
/// @ingroup gfx
/// Drawing the player's route and the target cursor's tile on the world map: a dot every `spacing` world units along the
/// route's line, a mark on its end and the outline of the cursor's tile. Pure: the renderer uploads what DrawRoute()
/// writes as one more sprite buffer, and tests read it without a device.

#include "core/manifest.hpp"
#include "core/vec2.hpp"
#include "core/world_look.hpp"
#include "gfx/terrain_draw.hpp"

#include <optional>
#include <span>

namespace gfx {

/// The most instances the route's buffer holds (an engine limit, so in code): the dots of a route across the widest map
/// and its two marks. A longer route loses its far dots, never its end.
inline constexpr u32 MAX_ROUTE_INSTANCES = 4'096;

/// Appends to `into` the drawing of `route` and of the tile of `cursorTile`, with one more batch over what it appends; none
/// of either draws nothing and adds no batch. In drawing order:
///
/// - a dot at `route`'s first point and one every `look.spacing` world units along the line through its points, up to its
///   end (a route of fewer than two points has no line);
/// - the end marker, centred on the route's last point;
/// - the cursor's outline, centred on `cursorTile` (the tile's centre).
///
/// Every sprite has its foot at the point it stands on and holds still. After MAX_ROUTE_INSTANCES minus the two marks' room
/// the dots stop, and the marks are still drawn.
void DrawRoute(
    std::span<const core::Vec2> route,
    std::optional<core::Vec2> cursorTile,
    const core::RouteLook& look,
    const core::Manifest& manifest,
    TerrainDrawing& into
);

} // namespace gfx
