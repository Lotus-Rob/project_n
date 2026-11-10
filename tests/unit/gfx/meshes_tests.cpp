#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/mesh_vertex.hpp"

#include <doctest/doctest.h>

namespace {

constexpr std::size_t FULL = std::size_t{ gfx::MAX_MESH_QUADS } * gfx::QUAD_VERTICES;

TEST_CASE("mesh quads past the buffer are counted, not drawn") {
    CHECK(gfx::MeshQuadsDropped(FULL + gfx::QUAD_VERTICES) == 1);
    CHECK(gfx::MeshQuadsDropped(FULL) == 0);
    CHECK(gfx::MeshQuadsDropped(0) == 0);
}

TEST_CASE("the mesh buffer holds 4 MiB of vertices") {
    CHECK(FULL * gfx::MESH_VERTEX_BYTES == std::size_t{ 4 } * 1024 * 1024);
}

} // namespace
