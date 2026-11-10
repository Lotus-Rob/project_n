#pragma once

/// @file
/// @ingroup gfx
/// The camera (design doc 0002 §8.10): which part of the world the window shows, and the
/// transform from world units to the GPU's clip space. Pure maths, tested without a device.

#include "core/types.hpp"
#include "core/vec2.hpp"

#include <cstddef>
#include <optional>

namespace gfx {

/// The reference resolution art is painted for: at zoom 1 the window shows this much of the
/// world on its shorter axis, whatever its size.
inline constexpr core::Vec2 REFERENCE_SIZE{ .x = 1920.0F, .y = 1080.0F };
/// The middle of the reference area, where a scene laid out in it is centred.
inline constexpr core::Vec2 REFERENCE_CENTRE{ .x = 960.0F, .y = 540.0F };

/// The smallest zoom of the range §8.10 sets, wide enough to show every mip level.
inline constexpr f32 MIN_ZOOM = 0.25F;
/// The largest zoom.
inline constexpr f32 MAX_ZOOM = 2.0F;

/// What the window shows: the world around `centre`, at `zoom`.
struct Camera {
    /// The world point at the window's centre.
    core::Vec2 centre = REFERENCE_CENTRE;
    /// How much larger than at the reference fit the world is drawn: above 1 shows less of it.
    f32 zoom = 1.0F;
};

/// How a frame shows a snapshot: the camera, the render clock and what the pointer hovers.
struct View {
    /// The render clock's progress from the snapshot's previous tick (0) to its own (1).
    f64 alpha = 1.0;
    /// What part of the world it shows.
    Camera camera{};
    /// The index in the snapshot's instances of the hovered entity's instance, which gets a pale
    /// outline (design doc 0002.3 §3); none when nothing is hovered.
    std::optional<std::size_t> outlined = std::nullopt;
    /// The render clock in seconds: its tick (core::RenderTick()) over core::TICKS_PER_SECOND. What fades and moves on
    /// screen alone (labels, damage numbers) times itself by it.
    f64 clock = 0.0;
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
/// window shows more of the world (§8.10); the camera's centre lands at the target's centre.
[[nodiscard]] ClipTransform WorldToClip(const Camera& camera, u32 width, u32 height);

/// The world point `transform` puts at clip-space position `clip`: WorldToClip()'s inverse.
[[nodiscard]] constexpr core::Vec2 ClipToWorld(const ClipTransform& transform, core::Vec2 clip) {
    return { .x = (clip.x - transform.offsetX) / transform.scaleX, .y = (clip.y - transform.offsetY) / transform.scaleY };
}

} // namespace gfx
