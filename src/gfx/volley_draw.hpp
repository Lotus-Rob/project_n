#pragma once

/// @file
/// @ingroup gfx
/// Drawing what the simulation reports but leaves to the main thread to place each frame (the phase 6 chunk 4 spec): the
/// flying swords at the render time, each with its halo and its trail, and the enemies' health bars. Pure: the renderer
/// uploads what these functions write as the overlay's sprite instances and the ribbons' vertices, and tests read them
/// without a device.

#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/manifest.hpp"
#include "core/types.hpp"
#include "core/ui_colours.hpp"
#include "core/volley.hpp"
#include "gfx/mesh_vertex.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace gfx {

/// How many quads each sword's trail has, between TRAIL_SEGMENTS + 1 points on its path.
inline constexpr u32 TRAIL_SEGMENTS = 8;
/// How far back in time a sword's trail reaches, in simulation ticks: its tail is where the sword was then. Chunk 11's
/// reduce motion shortens it.
inline constexpr f32 TRAIL_TICKS = 6.0F;
/// How bright the halos are, as a fraction of their element's colour: chunk 11's reduce flashing lowers it.
inline constexpr f32 HALO_INTENSITY = 1.0F;
/// How far over a creature's head its health bar's middle sits, in world units.
inline constexpr f32 HEALTH_BAR_RISE = 5.0F;
/// The health bar's background, in world units: its fill sits centred inside it.
inline constexpr f32 HEALTH_BAR_WIDTH = 44.0F;
/// The health bar background's height.
inline constexpr f32 HEALTH_BAR_HEIGHT = 6.0F;
/// The fill's length at full health, in world units.
inline constexpr f32 HEALTH_FILL_WIDTH = 40.0F;
/// The fill's height.
inline constexpr f32 HEALTH_FILL_HEIGHT = 4.0F;

/// The most instances the overlay's buffer holds: a halo and a sword for each of the most swords a volley has, and room
/// for the health bars.
inline constexpr u32 MAX_OVERLAY_INSTANCES = 4'096;
static_assert(2 * core::MAX_VOLLEY_COUNT <= MAX_OVERLAY_INSTANCES, "the overlay holds a halo and a sword per sword");
/// The most ribbon quads the trails' buffer holds: every trail of the largest volley.
inline constexpr u32 MAX_RIBBON_QUADS = 16'384;
static_assert(core::MAX_VOLLEY_COUNT * TRAIL_SEGMENTS <= MAX_RIBBON_QUADS, "the ribbons hold the largest volley's trails");

/// One corner of a trail's quad, as the ribbon pipeline reads it: the layout of MeshVertex (16 bytes), placed in world
/// units.
struct RibbonVertex {
    /// Its x in world units.
    f32 x = 0.0F;
    /// Its y in world units, growing downwards.
    f32 y = 0.0F;
    /// Where it is on the trail, two unorm16: u along it, from 0 at the sword to 1 at its tail, then v across it, from 0
    /// on one edge to 1 on the other.
    u32 uv = 0;
    /// The premultiplied colour the trail is drawn in: red in the low byte, then green, blue and alpha.
    u32 colour = 0;
};
static_assert(sizeof(RibbonVertex) == MESH_VERTEX_BYTES, "the ribbon pipeline reads vertices as the mesh path's");

/// What one frame draws over the snapshot: the overlay's sprite instances and their batches (the health bars, then every
/// sword's halo, then every sword), and the trails' quads (QUAD_VERTICES corners each, in MeshVertex's corner order). The
/// renderer keeps one and clears it each frame, so its buffers keep their capacity.
struct VolleyDrawing {
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The overlay's sprites, drawn in order with the overlay's alpha a no-op: each has `previous == current`.
    std::vector<core::Instance> instances{};
    /// The instances' batches, in drawing order, covering them all.
    std::vector<core::Batch> batches{};
    /// The trails' vertices, TRAIL_SEGMENTS quads a sword, in sword order: drawn additively before the instances.
    std::vector<RibbonVertex> ribbons{};
    // NOLINTEND(readability-redundant-member-init)

    /// Empties it, keeping its buffers' capacity.
    void Clear();
};

/// Adds a health bar for each of `bars` to `out`, as one batch of normal (premultiplied over) instances: a background of
/// HEALTH_BAR_WIDTH by HEALTH_BAR_HEIGHT in `colours`' BarBackground, and inside it a fill HEALTH_FILL_HEIGHT tall in its
/// EnemyHealthBar, as long as HEALTH_FILL_WIDTH times the bar's fraction (clamped to [0, 1]) and starting at the left,
/// both centred HEALTH_BAR_RISE world units over the head as `alpha` (0: previous tick, 1: this tick) puts it. Health
/// shows by the fill's length, whatever its colours. Draws `manifest`'s `solid` sprite, an 8 by 8 white square with its
/// foot at its left middle.
void DrawHealthBars(
    std::span<const core::HealthBar> bars,
    f32 alpha,
    const core::Manifest& manifest,
    const core::UiColours& colours,
    VolleyDrawing& out
);

/// Adds the swords of `volleys` (their targets in `targets`, as the snapshot holds them) to `out` at the render time
/// `core::RenderTick(simulationTick, alpha)`: each where core::SwordAt() puts it flying at core::SwordAim(), so a sword is
/// drawn where the simulation says it is, between ticks too. Every sword of every volley, in order, fills one halo, one
/// sword and TRAIL_SEGMENTS ribbon quads, whether it shows or not: one that doesn't (before its launch, after it struck a
/// living target at its arrival, or faded) writes zero instances and quads, so the bytes never depend on the worker count.
///
/// - The halos make one batch, additive, in `colours`' EffectColour() of the volley's element times HALO_INTENSITY and
///   how much of the sword shows (SwordPose::shown). The swords follow as one batch, normal: the ability picks only the
///   sprite (`sword` in its own steel, `fire_bolt` white tinted by the element's colour), faded as its pose says, turned
///   to its heading (the sprites point up).
/// - A trail runs back along the same path from the sword to where it was TRAIL_TICKS earlier, or at its launch if later,
///   in the element's colour, narrowing towards its tail. A sword that struck stops at its arrival and its trail drains
///   into its target.
///
/// A sword strikes when its target at its arrival (core::SwordTarget()) is a creature alive as that tick's arrivals
/// resolved (core::VolleyTarget::Struck()), and vanishes then; any other plunges, staying at its aim while it fades over
/// the shape's `fadeTicks`. Works on `jobs` in chunks of swords.
void DrawVolleys(
    std::span<const core::VolleyView> volleys,
    std::span<const core::VolleyTarget> targets,
    u64 simulationTick,
    f32 alpha,
    const core::Manifest& manifest,
    const core::UiColours& colours,
    core::Jobs& jobs,
    VolleyDrawing& out
);

} // namespace gfx
