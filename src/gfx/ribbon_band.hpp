#pragma once

/// @file
/// @ingroup gfx
/// One band of a ribbon laid along a curve, as the rivers and the roads both draw it. Pure.

#include "core/vec2.hpp"
#include "gfx/ribbon.hpp"

#include <vector>

namespace gfx {

/// A ribbon's midline and how far back from its end it fades out, in world units (0: it doesn't).
struct Laid {
    /// The points the quads' edges stand at, two or more.
    std::vector<core::Vec2> line;
    /// How far back from the end the band fades to nothing, in world units.
    f32 fade = 0.0F;
};

/// Adds the quads of one band of width `width` along `laid`'s line, in `colour`, fading out over its last stretch as
/// `laid` says: whole quads in MeshVertex's corner order, `u` running 0 to 1 along the line.
void AddBand(const Laid& laid, f32 width, u32 colour, std::vector<RibbonVertex>& into);

} // namespace gfx
