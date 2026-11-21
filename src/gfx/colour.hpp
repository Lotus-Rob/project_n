#pragma once

/// @file
/// @ingroup gfx
/// Colours as gfx hands them to its shaders. Pure maths, tested without a device.

#include "core/types.hpp"

#include <array>

namespace gfx {

/// The linear red, green and blue of the sRGB colour `srgb`, each the 8-bit linear byte core::FromSrgb() gives, over 255:
/// the precision the colour table and the sprites' colours have too, so a colour reads the same in every shader.
[[nodiscard]] std::array<f32, 3> LinearOf(const std::array<u8, 3>& srgb);

} // namespace gfx
