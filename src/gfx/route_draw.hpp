#pragma once

/// @file
/// @ingroup gfx
/// Drawing the player's route and the target cursor's tile on the world map: a dot every `spacing` world units along the
/// route's line and a mark on its end, and, laid out apart, the outline of the cursor's tile. Pure: the renderer uploads what
/// DrawRoute() and DrawTargetTile() write as more sprite buffers, and tests read it without a device.

#include "core/manifest.hpp"
#include "core/vec2.hpp"
#include "core/world_look.hpp"
#include "gfx/terrain_draw.hpp"

#include <optional>
#include <span>

namespace gfx {

/// The most instances the route's buffer holds (an engine limit, so in code): the dots of a route across the widest map
/// and its end marker. A longer route loses its far dots, never its end.
inline constexpr u32 MAX_ROUTE_INSTANCES = 4'096;

/// The most instances the target tile's buffer holds: the cursor's outline.
inline constexpr u32 MAX_TARGET_INSTANCES = 1;

/// Appends to `into` the drawing of `route`, with one more batch over what it appends; a route of fewer than two points
/// draws nothing and adds no batch. In drawing order:
///
/// - a dot at `route`'s first point and one every `look.spacing` world units along the line through its points, up to its
///   end;
/// - the end marker, centred on the route's last point.
///
/// Every sprite has its foot at the point it stands on and holds still. After MAX_ROUTE_INSTANCES minus the marker's room
/// the dots stop, and the marker is still drawn.
void DrawRoute(
    std::span<const core::Vec2> route,
    const core::RouteLook& look,
    const core::Manifest& manifest,
    TerrainDrawing& into
);

/// Appends to `into` the outline of the tile of `cursorTile` (the tile's centre) with its own batch; none draws nothing and
/// adds no batch. It is laid out apart from the route because it draws over the fog's clouds, which the route stays under.
void DrawTargetTile(
    std::optional<core::Vec2> cursorTile,
    const core::RouteLook& look,
    const core::Manifest& manifest,
    TerrainDrawing& into
);

} // namespace gfx
