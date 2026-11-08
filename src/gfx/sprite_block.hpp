#pragma once

/// @file
/// @ingroup gfx
/// What a sprite writes onto its atlas page (design doc 0002 §8.7). Pure, tested without a device.

#include "core/manifest.hpp"
#include "core/types.hpp"

#include <cstddef>
#include <span>

namespace gfx {

/// R8G8B8A8.
inline constexpr std::size_t BYTES_PER_TEXEL = 4;

/// A rectangle of texels on an atlas page.
struct SpriteBlock {
    /// Its left edge.
    u32 x = 0;
    /// Its top edge.
    u32 y = 0;
    /// Its width.
    u32 width = 0;
    /// Its height.
    u32 height = 0;
};

/// The texels of its page that `sprite` writes in an array of `mipLevels` (at least 2): its image
/// and its half of the gutter around it, so that the lower mips and filtering find its own colour
/// past its edges, not an empty page.
[[nodiscard]] SpriteBlock BlockOf(const core::SpriteInfo& sprite, u32 mipLevels);

/// Writes BlockOf(sprite, mipLevels) into `destination`, row after row without gaps, from the sprite's image
/// `pixels`, whose rows start `pitch` bytes apart, repeating the image's edges out to the block's.
/// `destination` holds exactly the block.
void WriteBlock(
    const core::SpriteInfo& sprite,
    u32 mipLevels,
    std::span<const std::byte> pixels,
    std::size_t pitch,
    std::span<std::byte> destination
);

} // namespace gfx
