#pragma once

/// @file
/// @ingroup gfx
/// What the world map's ground and river pipelines draw by: the ground shader's uniforms from the map and the look, the
/// part of the view the ground covers, and the rivers' painted shape. Pure, tested without a device.

#include "core/rect.hpp"
#include "core/types.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"
#include "gfx/river_draw.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace gfx {

/// GroundUniforms::mode: the painted ground, opaque (paper, washes, water, shore, grid).
inline constexpr u32 GROUND_MODE_GROUND = 0;
/// GroundUniforms::mode: only the veil's pale patches, laid over what is drawn already.
inline constexpr u32 GROUND_MODE_VEIL = 1;
/// GroundUniforms::mode: only the paper's colour, opaque.
inline constexpr u32 GROUND_MODE_PAPER = 2;

/// What the ground fragment shader paints with: `Ground` in shaders/ground.hlsli, rows of 16 bytes. Colours are linear
/// red, green and blue; lengths in tiles unless a field says otherwise.
struct GroundUniforms {
    /// The map's tiles across, then down.
    std::array<f32, 2> mapSize{};
    /// A tile's side in world units.
    f32 tileSize = 0.0F;
    /// What the draw paints: GROUND_MODE_GROUND, GROUND_MODE_VEIL or GROUND_MODE_PAPER.
    u32 mode = GROUND_MODE_GROUND;
    /// The paper's colour.
    std::array<f32, 3> paper{};
    /// The paper fibre's strength, from 0 to 1.
    f32 grain = 0.0F;
    /// How far noise moves the edge between two kinds.
    f32 edgeWarp = 0.0F;
    /// How wide that edge blends (a tile at most: wider blends as a tile).
    f32 edgeSoft = 0.0F;
    /// How unevenly a wash pools, from 0 to 1.
    f32 blotch = 0.0F;
    /// How far from the shore water turns from shallow to deep.
    f32 depthTiles = 0.0F;
    /// The shore line's colour.
    std::array<f32, 3> shore{};
    /// How strongly the shore line shows, from 0 to 1.
    f32 shoreStrength = 0.0F;
    /// The grid's colour.
    std::array<f32, 3> grid{};
    /// How strongly the grid shows, from 0 to 1.
    f32 gridStrength = 0.0F;
    /// The veil's colour.
    std::array<f32, 3> veil{};
    /// How strongly the veil shows, from 0 to 1.
    f32 veilStrength = 0.0F;
    /// The map's kind index of shallow water.
    u32 shallowKind = 0;
    /// The map's kind index of deep water.
    u32 deepKind = 0;
    /// The shore line's width.
    f32 shoreWidth = 0.0F;
    /// The size of the veil's patches.
    f32 veilScale = 0.0F;
    /// The zoom where the grid is whole, then the zoom where it is gone.
    std::array<f32, 2> gridFade{};
    /// The camera's zoom, which the grid fades by: GroundUniformsOf() gives 1, and the renderer sets each frame's.
    f32 zoom = 1.0F;
    /// Pads the row.
    f32 padding = 0.0F;
    /// Each kind's wash by the map's kind index: linear red, green and blue, then its strength; zero past the look's kinds.
    std::array<std::array<f32, 4>, core::MAX_GROUND_KINDS> washes{};
};
/// A row of the shader's constant buffer.
inline constexpr std::size_t GROUND_ROW_BYTES = 16;
/// The rows before the washes.
inline constexpr std::size_t GROUND_VALUE_ROWS = 8;
/// The bytes of GroundUniforms, as the shader's constant buffer holds them.
inline constexpr std::size_t GROUND_UNIFORM_BYTES = (GROUND_VALUE_ROWS + core::MAX_GROUND_KINDS) * GROUND_ROW_BYTES;
static_assert(sizeof(GroundUniforms) == GROUND_UNIFORM_BYTES, "the shader's ground constant buffer is 24 rows");
static_assert(
    offsetof(GroundUniforms, paper) == GROUND_ROW_BYTES &&
        offsetof(GroundUniforms, edgeWarp) == offsetof(GroundUniforms, paper) + GROUND_ROW_BYTES &&
        offsetof(GroundUniforms, shore) == offsetof(GroundUniforms, edgeWarp) + GROUND_ROW_BYTES &&
        offsetof(GroundUniforms, grid) == offsetof(GroundUniforms, shore) + GROUND_ROW_BYTES &&
        offsetof(GroundUniforms, veil) == offsetof(GroundUniforms, grid) + GROUND_ROW_BYTES &&
        offsetof(GroundUniforms, shallowKind) == offsetof(GroundUniforms, veil) + GROUND_ROW_BYTES &&
        offsetof(GroundUniforms, gridFade) == offsetof(GroundUniforms, shallowKind) + GROUND_ROW_BYTES &&
        offsetof(GroundUniforms, washes) == GROUND_VALUE_ROWS * GROUND_ROW_BYTES,
    "the shader's ground constant buffer starts each group on its own 16-byte row"
);

/// The uniforms that paint `map`'s ground in `look`'s colours and strengths, drawing `mode`, at zoom 1.
[[nodiscard]] GroundUniforms GroundUniformsOf(const core::WorldMap& map, const core::WorldLook& look, u32 mode);

/// The part of `view` that `map` covers (from the origin to its columns and rows of tiles), which the ground pass draws;
/// none when they share no area, so nothing is drawn past the map or when the view misses it.
[[nodiscard]] std::optional<core::Rect> GroundQuad(core::Rect view, const core::WorldMap& map);

/// The rivers' painted shape for `look` on `map`: its widths in world units, the core opaque in its colour and the edge
/// in its colour at its strength, premultiplied.
[[nodiscard]] RiverShape RiverShapeOf(const core::WorldMap& map, const core::RiverLook& look);

} // namespace gfx
