#pragma once

/// @file
/// @ingroup gfx
/// One band of a ribbon laid along a curve, as the rivers and the roads both draw it. Pure.

#include "core/vec2.hpp"
#include "gfx/ribbon.hpp"

#include <vector>

namespace gfx {

/// A ribbon's midline and how its start and end are shaped, in world units (0: not shaped).
struct Laid {
    /// The points the quads' edges stand at, two or more.
    std::vector<core::Vec2> line;
    /// How far back from the end the band fades to nothing, in world units.
    f32 fade = 0.0F;
    /// How far from the start the band rises from nothing to its colour, in world units; a line shorter than twice this
    /// rises over half its length.
    f32 fadeIn = 0.0F;
    /// How far from the start both sides widen from nothing to the full width, in world units; a line shorter than twice
    /// this tapers over half its length.
    f32 taper = 0.0F;
};

/// Adds the quads of one band of width `width` along `laid`'s line, in `colour`, fading out over its last stretch as
/// `laid` says, and rising from nothing and widening from a point over its first: whole quads in MeshVertex's corner order, `u`
/// running 0 to 1 along the line.
void AddBand(const Laid& laid, f32 width, u32 colour, std::vector<RibbonVertex>& into);

} // namespace gfx
