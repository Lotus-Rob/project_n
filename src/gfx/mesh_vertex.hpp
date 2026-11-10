#pragma once

/// @file
/// @ingroup gfx
/// What the mesh path draws (design doc 0002.4.2 §2): the vertices of textured, coloured quads placed in target pixels,
/// and how many quads its buffer holds. Pure, tested without a device.

#include "core/types.hpp"

#include <cstddef>

namespace gfx {

/// One corner of a quad as the vertex shader reads it (`VertexIn` in shaders/mesh.vert.hlsl): 16 bytes.
struct MeshVertex {
    /// Its x in target pixels, from the left.
    f32 x = 0.0F;
    /// Its y in target pixels, from the top.
    f32 y = 0.0F;
    /// Its place on the page as fractions of the page: u, then v, unorm16.
    u32 uv = 0;
    /// Premultiplied colour the text is drawn in: red in the low byte, then green, blue and alpha.
    u32 colour = 0;
};
/// The bytes of one MeshVertex, which the pipeline's vertex attributes read with the same layout.
inline constexpr std::size_t MESH_VERTEX_BYTES = 16;
static_assert(sizeof(MeshVertex) == MESH_VERTEX_BYTES, "the pipeline reads mesh vertices as 16 bytes");

/// The corners of a quad, in the order its indices name them: top left, top right, bottom left, bottom right.
inline constexpr u32 QUAD_VERTICES = 4;
/// The most quads the vertex buffer holds: 4 MiB of vertices, sized for thousands of labels and numbers at once.
inline constexpr u32 MAX_MESH_QUADS = 65'536;

/// How many of the quads in `vertices` vertices (whole quads) don't fit in the buffer and aren't drawn.
[[nodiscard]] constexpr std::size_t MeshQuadsDropped(std::size_t vertices) {
    const std::size_t quads = vertices / QUAD_VERTICES;
    return quads > MAX_MESH_QUADS ? quads - MAX_MESH_QUADS : 0;
}

} // namespace gfx
