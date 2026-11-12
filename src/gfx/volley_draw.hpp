#pragma once

/// @file
/// @ingroup gfx
/// Drawing what the simulation reports but leaves to the main thread to place each frame (the phase 6 chunk 4 spec): the
/// flying swords at the render time, each wearing its glow and with its trail, the poison lance's bursts, and the enemies'
/// health bars. Pure: the renderer uploads what these functions write as the overlay's sprite instances and the ribbons'
/// vertices, and tests read them without a device.

#include "core/effects.hpp"
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/manifest.hpp"
#include "core/types.hpp"
#include "core/ui_colours.hpp"
#include "core/volley.hpp"
#include "gfx/hud_draw.hpp"
#include "gfx/labels.hpp"
#include "gfx/mesh_vertex.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace gfx {

/// How many quads each sword's trail has, between TRAIL_SEGMENTS + 1 points on its path. In code, not in effects.yaml: it
/// sizes the ribbon buffers.
inline constexpr u32 TRAIL_SEGMENTS = 8;
/// The instances the overlay draws for each sword: itself, wearing its glow. An engine limit, so in code.
inline constexpr u32 SWORD_INSTANCES = 1;
/// The most instances the overlay's buffer holds (an engine limit, so in code): every sword of the largest volley, and room
/// for bolts, bursts, health bars and the HUD.
inline constexpr u32 MAX_OVERLAY_INSTANCES = 8'192;
static_assert(SWORD_INSTANCES * core::MAX_VOLLEY_COUNT <= MAX_OVERLAY_INSTANCES, "the overlay holds the largest volley");
/// The most ribbon quads the trails' buffer holds (an engine limit, so in code): every trail of the largest volley.
inline constexpr u32 MAX_RIBBON_QUADS = 16'384;
static_assert(core::MAX_VOLLEY_COUNT * TRAIL_SEGMENTS <= MAX_RIBBON_QUADS, "the ribbons hold the largest volley's trails");
/// The ribbon vertices each sword's trail takes: QUAD_VERTICES for each of its TRAIL_SEGMENTS quads.
inline constexpr u32 TRAIL_VERTICES = TRAIL_SEGMENTS * QUAD_VERTICES;

/// How many whole swords (blade and trail) still fit beside `instances` overlay instances and `ribbonVertices` ribbon vertices
/// already placed, in MAX_OVERLAY_INSTANCES and MAX_RIBBON_QUADS: DrawVolleys() draws no more.
[[nodiscard]] constexpr std::size_t SwordRoom(std::size_t instances, std::size_t ribbonVertices) {
    constexpr std::size_t RIBBON_VERTICES = std::size_t{ MAX_RIBBON_QUADS } * QUAD_VERTICES;
    const std::size_t overlayRoom =
        (MAX_OVERLAY_INSTANCES - std::min<std::size_t>(instances, MAX_OVERLAY_INSTANCES)) / SWORD_INSTANCES;
    const std::size_t trailRoom = (RIBBON_VERTICES - std::min(ribbonVertices, RIBBON_VERTICES)) / TRAIL_VERTICES;
    return std::min(overlayRoom, trailRoom);
}
/// The most swords one frame draws, with nothing else placed: SwordRoom() of an empty frame.
inline constexpr std::size_t MAX_DRAWN_SWORDS = SwordRoom(0, 0);

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

/// What one frame draws over the snapshot: the overlay's sprite instances and their batches, the HUD's batches among the same
/// instances, and the trails' quads (QUAD_VERTICES corners each, in MeshVertex's corner order). The batches draw every sword,
/// then the bursts, then the health bars, in world units; the HUD's draw its bars in target pixels, over the world's texts. The
/// renderer keeps one and clears it each frame, so its buffers keep their capacity.
struct VolleyDrawing {
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The overlay's sprites, drawn as the batches say, with the overlay's alpha a no-op: each has `previous == current`.
    std::vector<core::Instance> instances{};
    /// The instances' batches, in drawing order, covering them all; not in the instances' order.
    std::vector<core::Batch> batches{};
    /// The trails' vertices, TRAIL_SEGMENTS quads a sword, in sword order: drawn additively before the instances.
    std::vector<RibbonVertex> ribbons{};
    /// The HUD's batches (DrawHudBars()), placed in target pixels: not among `batches`.
    std::vector<core::Batch> hud{};
    // NOLINTEND(readability-redundant-member-init)
    /// Swords left out, blade and trail, as the overlay or the trails had no room for them.
    std::size_t droppedSwords = 0;

    /// Empties it, keeping its buffers' capacity.
    void Clear();
};

/// How much of a creature's health bar shows, from its name label as the label pass placed it this frame: the label's
/// opacity (it fades when too small to read, or hidden), less any part while it heads a stack ("Name (+N)", which names
/// several creatures: their bars would stack into a block), cross-fading with its count.
[[nodiscard]] f32 BarOpacity(const PlacedLabel& label);

/// How much of one creature's health bar shows this frame (BarOpacity()).
struct BarShown {
    /// The creature.
    core::StableId entity{};
    /// How much of its bar shows, from 0 to 1.
    f32 opacity = 0.0F;
};

/// Adds a health bar for each of `bars` to `out`, as one batch of normal (premultiplied over) instances: a background of
/// `effects`' health bar width by height in `colours`' BarBackground, and inside it a fill of its fill height in its
/// EnemyHealthBar, as long as its fill width times the bar's fraction (clamped to [0, 1], NaN as 0) and starting at the
/// left, both centred its rise in world units over the head as `alpha` (0: previous tick, 1: this tick) puts it.
/// Health shows by the fill's length, whatever its colours. A bar follows its creature's label: it is drawn at its
/// `shown` opacity (sorted by entity), and not at all for a creature not there or at 0. Draws `manifest`'s `solid`
/// sprite, an 8 by 8 white square with its foot at its left middle.
void DrawHealthBars(
    std::span<const core::HealthBar> bars,
    std::span<const BarShown> shown,
    f32 alpha,
    const core::Manifest& manifest,
    const core::UiColours& colours,
    const core::Effects& effects,
    VolleyDrawing& out
);

/// Adds each of `bars` to `out` in target pixels, as one batch of normal instances in `out.hud`: `manifest`'s `solid`
/// sprite (an 8 by 8 white square, its foot at its left middle) stretched over the bar in its background colour, then
/// over its left part, as long as its fraction says (clamped to [0, 1], NaN as 0; none at 0), in its fill colour. Called
/// first in a frame, so the HUD's room is taken before the world's effects take theirs (SwordRoom() counts it).
void DrawHudBars(std::span<const HudBar> bars, const core::Manifest& manifest, VolleyDrawing& out);

/// Adds each of `flashes` showing at render tick `renderTick` to `out`, as one batch of additive instances: `manifest`'s
/// `lance_strike` (white) centred on its point, in `colours`' EffectColour() of its element times the lance's
/// core::BurstLook::intensity in `effects`. Over its own `ticks` it grows from the look's start scale to its end scale,
/// slowing as it goes, and fades to nothing; it shows from its start tick (`renderTick` at it) to the end, and not at all
/// outside them, so it never flashes back.
void DrawFlashes(
    std::span<const core::Flash> flashes,
    f64 renderTick,
    const core::Manifest& manifest,
    const core::UiColours& colours,
    const core::Effects& effects,
    VolleyDrawing& out
);

/// Adds the swords of `volleys` (their targets in `targets`, as the snapshot holds them) to `out` at the render time
/// `core::RenderTick(simulationTick, alpha)`: each where core::SwordAt() puts it flying at core::SwordAim(), so a sword is
/// drawn where the simulation says it is, between ticks too. Every sword of every volley, in order, fills one instance and
/// TRAIL_SEGMENTS ribbon quads, whether it shows or not: one that doesn't (before its launch, after it struck a living
/// target at its arrival, or faded) writes a zero instance and quads, so the bytes never depend on the worker count.
/// Swords that don't fit beside what `out` holds (SwordRoom()) are left out whole and counted in `droppedSwords`: no blade
/// is drawn without its trail.
///
/// - The swords make one batch, normal, drawn before every batch already in `out` (the bursts and the health bars, which
///   stay readable over them): the ability picks the sprite (`sword` in its own steel, `fire_bolt` white tinted by
///   `colours`' EffectColour() of the volley's element), at its look's sword scale (core::Effects::LookOf()), faded by how
///   much of it shows (SwordPose::shown), turned to its heading (the sprites point up). Each wears its role's glow
///   (core::GlowRole::FlyingSwords, or FireBolt for the bolt) in the volley's element, its pulse counted from its own launch
///   tick; the glow fades with the square of what shows, so a plunged sword's glow goes before its blade.
/// - A trail runs back along the same path from the sword to where it was the look's trail ticks earlier, or where it left
///   its ring if later (a sword still on its ring has none), in the element's colour times its trail intensity and how much
///   shows, narrowing by its taper towards its tail, at its width. A sword that struck stops at its arrival and its trail
///   drains into its target.
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
    const core::Effects& effects,
    core::Jobs& jobs,
    VolleyDrawing& out
);

} // namespace gfx
