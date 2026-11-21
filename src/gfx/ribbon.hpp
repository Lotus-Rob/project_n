#pragma once

/// @file
/// @ingroup gfx
/// What the ribbon pipeline's vertices have in common, whatever lays the ribbon out (a sword's trail, a river): the
/// vertex, the order its quad's corners go in and how a ribbon's sides stand off its centre line. Pure.

#include "core/instances.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "gfx/mesh_vertex.hpp"

#include <cmath>
#include <span>

namespace gfx {

/// One corner of a ribbon's quad (a sword's trail, a river), as the ribbon pipeline reads it: the layout of MeshVertex (16
/// bytes), placed in world units.
struct RibbonVertex {
    /// Its x in world units.
    f32 x = 0.0F;
    /// Its y in world units, growing downwards.
    f32 y = 0.0F;
    /// Where it is on the ribbon, two unorm16: u along it, from 0 at its start to 1 at its end, then v across it, from 0
    /// on one edge to 1 on the other.
    u32 uv = 0;
    /// The premultiplied colour the ribbon is drawn in: red in the low byte, then green, blue and alpha.
    u32 colour = 0;
};
static_assert(sizeof(RibbonVertex) == MESH_VERTEX_BYTES, "the ribbon pipeline reads vertices as the mesh path's");

/// A tangent shorter than this, in world units, is too short to say which way a ribbon runs there.
inline constexpr f32 SHORTEST_TANGENT = 1.0e-4F;

/// A ribbon's half: each side stands half its width from the centre line.
inline constexpr f32 RIBBON_HALF = 0.5F;

/// The length of `vec`.
[[nodiscard]] inline f32 Length(core::Vec2 vec) {
    return std::hypot(vec.x, vec.y);
}

/// The unit vector along `runs`, or `fallback` where `runs` is too short to give a direction.
[[nodiscard]] inline core::Vec2 HeadingOf(core::Vec2 runs, core::Vec2 fallback) {
    const f32 length = Length(runs);
    return length > SHORTEST_TANGENT ? runs * (1.0F / length) : fallback;
}

/// How far the left side of a ribbon of `width` stands from its centre line where it runs along the unit vector
/// `heading` (the right side stands the same distance the other way).
[[nodiscard]] inline core::Vec2 SideOf(core::Vec2 heading, f32 width) {
    return core::Vec2{ .x = -heading.y, .y = heading.x } * (width * RIBBON_HALF);
}

/// One ribbon vertex at `position`, `along` and `across` (each in [0, 1]) packed as the pipeline reads them.
[[nodiscard]] inline RibbonVertex MakeRibbonVertex(core::Vec2 position, f32 along, f32 across, u32 colour) {
    return RibbonVertex{ .x = position.x,
                         .y = position.y,
                         .uv = core::PackPair(core::PackUnorm16(along), core::PackUnorm16(across)),
                         .colour = colour };
}

/// Where one end of a quad stands: its two sides, and how far along the ribbon it is.
struct RibbonEnd {
    /// The side `v` 0 is on.
    core::Vec2 left{};
    /// The side `v` 1 is on.
    core::Vec2 right{};
    /// How far along the ribbon it is, from 0 to 1.
    f32 along = 0.0F;
};

/// Writes the QUAD_VERTICES corners of the quad from `near` to `far` into `corners`, in MeshVertex's corner order.
inline void WriteRibbonQuad(std::span<RibbonVertex> corners, const RibbonEnd& near, const RibbonEnd& far, u32 colour) {
    corners[0] = MakeRibbonVertex(near.left, near.along, 0.0F, colour);
    corners[1] = MakeRibbonVertex(near.right, near.along, 1.0F, colour);
    corners[2] = MakeRibbonVertex(far.left, far.along, 0.0F, colour);
    corners[3] = MakeRibbonVertex(far.right, far.along, 1.0F, colour);
}

} // namespace gfx
