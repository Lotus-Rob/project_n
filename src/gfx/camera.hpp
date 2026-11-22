#pragma once

/// @file
/// @ingroup gfx
/// The camera: which part of the world the window shows, and the
/// transform from world units to the GPU's clip space. Pure maths, tested without a device.

#include "core/types.hpp"
#include "core/vec2.hpp"
#include "core/view_limits.hpp"
#include "gfx/fog.hpp"

#include <cstddef>
#include <optional>

namespace gfx {

/// The reference resolution art is painted for (core::REFERENCE_SIZE).
using core::REFERENCE_SIZE;
/// The middle of the reference area, where a scene laid out in it is centred.
inline constexpr core::Vec2 REFERENCE_CENTRE{ .x = 960.0F, .y = 540.0F };

/// The smallest zoom (core::MIN_ZOOM).
using core::MIN_ZOOM;
/// The largest zoom (core::MAX_ZOOM).
using core::MAX_ZOOM;

/// What the window shows: the world around `centre`, at `zoom`.
struct Camera {
    /// The world point at the window's centre.
    core::Vec2 centre = REFERENCE_CENTRE;
    /// How much larger than at the reference fit the world is drawn: above 1 shows less of it.
    f32 zoom = 1.0F;
};

/// Which parts of the world map's terrain a frame draws. Only the shimmer measure sets anything but All: it holds the
/// painted ground's crawl against the same frame as flat paper, without the road ribbons, whose shimmer is their own.
enum class TerrainParts : u8 {
    All,         ///< The ground, rivers, roads, every terrain sprite and the veil.
    AllButRoads, ///< All but the road ribbons.
    PaperOnly,   ///< The ground as flat paper (GROUND_MODE_PAPER), and nothing else of the terrain.
};

/// How a frame shows a snapshot: the camera, the render clock, the frame clock and what the pointer hovers.
struct View {
    /// The render clock's progress from the snapshot's previous tick (0) to its own (1).
    f64 alpha = 1.0;
    /// What part of the world it shows.
    Camera camera{};
    /// The index in the snapshot's instances of the hovered entity's instance, which gets a pale
    /// outline; none when nothing is hovered.
    std::optional<std::size_t> outlined = std::nullopt;
    /// The main thread's frame clock in seconds (app::FrameClock): wall time, which runs on while the game is paused,
    /// unfocused or loading a scene, when the render clock holds. Labels fade, cross-fade and glide by it, so they settle
    /// while the player zooms or hovers a paused game; damage numbers are timed by the render clock before they reach the
    /// renderer, and freeze with the game.
    f64 frameClock = 0.0;
    /// Whether it draws the seals a scene shows as its content (core::Seal::showcase): the arena's showcase, which a debug
    /// key hides and shows. Seals raised in play draw either way.
    bool showcase = true;
    /// The fog over the world map; none (no columns) elsewhere.
    FogFrame fog{};
    /// Which parts of the world map's terrain it draws: all of them but for the shimmer measure (TerrainParts).
    TerrainParts terrainParts = TerrainParts::All;
};

/// `zoom` after `notches` of the mouse wheel: each one multiplies it by the same factor, and the
/// result stays within MIN_ZOOM and MAX_ZOOM.
[[nodiscard]] f32 ZoomedBy(f32 zoom, f32 notches);

/// Clip-space position = world position × scale + offset, per axis. The shader's camera uniform.
struct ClipTransform {
    /// Clip x per world unit.
    f32 scaleX = 0.0F;
    /// Clip y per world unit; negative, as world y grows downwards and clip y upwards.
    f32 scaleY = 0.0F;
    /// Clip x at world x 0.
    f32 offsetX = 0.0F;
    /// Clip y at world y 0.
    f32 offsetY = 0.0F;
};

/// How many target pixels a world unit covers with `camera` on a target `width` by `height` pixels: the fit of
/// REFERENCE_SIZE to the target (the smaller of the two axis ratios) times the zoom, as WorldToClip() places the world.
[[nodiscard]] f32 PixelsPerUnit(const Camera& camera, u32 width, u32 height);

/// The transform for `camera` on a target `width` by `height` pixels: REFERENCE_SIZE scaled by
/// the smaller of the two axis ratios fits the target, so the aspect ratio holds and a wider
/// window shows more of the world; the camera's centre lands at the target's centre.
[[nodiscard]] ClipTransform WorldToClip(const Camera& camera, u32 width, u32 height);

/// The world point `transform` puts at clip-space position `clip`: WorldToClip()'s inverse.
[[nodiscard]] constexpr core::Vec2 ClipToWorld(const ClipTransform& transform, core::Vec2 clip) {
    return { .x = (clip.x - transform.offsetX) / transform.scaleX, .y = (clip.y - transform.offsetY) / transform.scaleY };
}

} // namespace gfx
