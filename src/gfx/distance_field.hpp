#pragma once

/// @file
/// @ingroup gfx
/// A sprite's signed distance field: how far each texel lies from the sprite's edge, for the glows
/// (spec 2026-11-12-phase6-glow-design.md, "Distance fields"). Pure, tested without a device.

#include "core/types.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace gfx {

/// The byte of a signed distance of `texels` (negative inside the sprite): 255 x clamp(0.5 + texels / 32, 0, 1),
/// rounded, so that 128 is the edge and the field reaches 16 texels each way.
[[nodiscard]] u8 EncodeDistance(f32 texels);

/// The encoded field of an image's alpha (128 and up is inside) for the block of (width + 32) x (height + 32)
/// texels, rows without gaps, the image at (16, 16). `rgba` holds the image's rows `pitch` bytes apart. An outside
/// texel holds its distance to the nearest inside texel's centre less half a texel; an inside texel holds the
/// negated distance to the nearest outside texel's centre less half a texel. Texels beyond the image are outside.
/// Linear in the texels.
[[nodiscard]] std::vector<u8> DistanceField(std::span<const std::byte> rgba, std::size_t pitch, u32 width, u32 height);

} // namespace gfx
