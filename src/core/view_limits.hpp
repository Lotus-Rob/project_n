#pragma once

/// @file
/// @ingroup core
/// How much of the world a window can show (design doc 0002 §8.10): the reference size art is painted for and the zoom
/// range. Core holds them so the logic can cull as widely as any window shows without the main thread's camera.

#include "core/types.hpp"
#include "core/vec2.hpp"

namespace core {

/// The reference resolution art is painted for: at zoom 1 the window shows this much of the world on its shorter axis,
/// whatever its size.
inline constexpr Vec2 REFERENCE_SIZE{ .x = 1920.0F, .y = 1080.0F };

/// The smallest zoom of the range §8.10 sets, wide enough to show every mip level.
inline constexpr f32 MIN_ZOOM = 0.25F;
/// The largest zoom.
inline constexpr f32 MAX_ZOOM = 2.0F;

/// Half the world a window shows at MIN_ZOOM, at its widest: a 32:9 window shows 15,360 world units across (half of it
/// on x), an upright 8:9 one 8,640 high (half of it on y). What a publish culls to round where the camera starts before
/// the main thread has sent its view (core::ViewReport).
inline constexpr Vec2 WIDEST_HALF_VIEW = REFERENCE_SIZE * (1.0F / MIN_ZOOM);

} // namespace core
