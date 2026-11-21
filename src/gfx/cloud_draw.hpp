#pragma once

/// @file
/// @ingroup gfx
/// Laying out the clouds over the world map's unexplored land from the eased fog: a lattice of cells half a tile across
/// at level 0, each level up doubling them, so the clouds keep about the same size on screen at any zoom. Pure: the
/// renderer uploads what DrawClouds() writes as one more sprite buffer, and tests read it without a device.

#include "core/frame.hpp"
#include "core/manifest.hpp"
#include "core/rect.hpp"
#include "core/types.hpp"
#include "core/world_look.hpp"
#include "gfx/fog.hpp"

#include <vector>

namespace gfx {

/// The most instances the clouds' buffer holds (an engine limit, so in code): the clouds of the widest view at any zoom,
/// two levels at once while a zoom step cross-fades, with room to spare (a test holds the shipped map to it).
inline constexpr u32 MAX_CLOUD_INSTANCES = 8'192;

/// What the clouds draw: their sprite instances and the batch that draws them, in world units. The renderer keeps one, so
/// its buffers keep their capacity.
struct CloudDrawing {
    /// The instances, back to front, each with `previous == current`: the clouds move with the render time, not the tick.
    std::vector<core::Instance> instances;
    /// One normal-blend batch covering every instance, or none when there are none.
    std::vector<core::Batch> batches;
};

/// The continuous cloud level at `zoom`: log2(1 / `zoom`), never under 0, so level 0 from zoom 1 inward and each halving of
/// the zoom one level up.
[[nodiscard]] f32 CloudLevel(f32 zoom);

/// Replaces `into` with the clouds over the unexplored land `view` shows at `zoom`, at `seconds` of render time; nothing
/// with no fog (FogFrame::columns 0) or no cloud sprites in `look`.
///
/// A cell at level `l` is 0.5 × 2^l tiles across, counted from the map's origin, and covers its tiles' cloud as a whole
/// (1 minus the largest eased explored value of the tiles it touches, tiles off the map counting as explored) or hands what
/// it doesn't cover to its four children, down to level 0; so a cell next to explored land splits into finer clouds, and
/// every split, merge and uncover cross-fades as the fog eases, with no state kept between calls. Across the middle of
/// each zoom step (0.35 to 0.65 of the way between two levels) the coarser level fades in as the finer fades out.
///
/// Each cell's two or three clouds come from a hash of its column, row and level: their places, widths, sprites and mirror
/// flips, so the same view, or one panned away and back, gives the same clouds. A cloud whose box would reach past its
/// cell's tiles into explored land or off the map is moved in, then shrunk, so its box (with its bob) stays inside them;
/// over a tile being uncovered it is pulled in only as far as the tile's eased value, so it eases in rather than jumping.
/// Each bobs up and down by FogLook::bobAmplitude of its cell with its own phase, and drifts its own way as its cell is
/// uncovered.
///
/// Instances run back to front by the foot each cloud rests at (before its bob and drift, so neither swaps two), all in
/// one normal-blend batch. Past MAX_CLOUD_INSTANCES the farthest back are dropped.
void DrawClouds(
    const FogFrame& fog,
    const core::FogLook& look,
    const core::Manifest& manifest,
    core::Rect view,
    f32 zoom,
    f64 seconds,
    CloudDrawing& into
);

} // namespace gfx
