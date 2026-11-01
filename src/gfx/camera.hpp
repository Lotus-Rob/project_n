#pragma once

/// @file
/// @ingroup gfx
/// The camera (design doc 0002 §8.10): which part of the world the window shows, and the
/// transform from world units to the GPU's clip space. Pure maths, tested without a device.

#include "core/types.hpp"
#include "core/vec2.hpp"

namespace gfx {

/// The reference resolution art is painted for: at zoom 1 the window shows this much of the
/// world on its shorter axis, whatever its size.
inline constexpr core::Vec2 REFERENCE_SIZE{ .x = 1920.0F, .y = 1080.0F };
/// The middle of the reference area, where a scene laid out in it is centred.
inline constexpr core::Vec2 REFERENCE_CENTRE{ .x = 960.0F, .y = 540.0F };

/// What the window shows: the world around `centre`, at `zoom`.
struct Camera {
    /// The world point at the window's centre.
    core::Vec2 centre = REFERENCE_CENTRE;
    /// How much larger than at the reference fit the world is drawn: above 1 shows less of it.
    f32 zoom = 1.0F;
};

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

/// The transform for `camera` on a target `width` by `height` pixels: REFERENCE_SIZE scaled by
/// the smaller of the two axis ratios fits the target, so the aspect ratio holds and a wider
/// window shows more of the world (§8.10); the camera's centre lands at the target's centre.
[[nodiscard]] ClipTransform WorldToClip(const Camera& camera, u32 width, u32 height);

} // namespace gfx
