#pragma once

/// @file
/// @ingroup gfx
/// Laying the world map's roads out as quiet painted bands along their curves. Pure: the renderer builds the vertices once
/// for a map and a look, and tests read them without a device.

#include "core/types.hpp"
#include "core/vec2.hpp"
#include "gfx/ribbon.hpp"

#include <span>
#include <vector>

namespace gfx {

/// The most quads the roads' buffer holds, both bands together (an engine limit, so in code): DrawRoads() leaves out the
/// roads that don't fit.
inline constexpr u32 MAX_ROAD_QUADS = 8'192;

/// Replaces `into` with the quads of every road's edge band, then of every road's core band, each a ribbon along its curve
/// in `curves` (core::RoadCurves()), as whole quads in MeshVertex's corner order with `u` running 0 to 1 along the road and
/// `v` 0 on one side and 1 on the other, one quad for each step of the curve. A curve of fewer than two points draws
/// nothing; roads that would take the quads past MAX_ROAD_QUADS are left out whole, the later ones first.
void DrawRoads(std::span<const std::vector<core::Vec2>> curves, const RibbonShape& shape, std::vector<RibbonVertex>& into);

} // namespace gfx
