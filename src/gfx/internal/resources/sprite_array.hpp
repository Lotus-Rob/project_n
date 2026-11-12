#pragma once

/// @file
/// @ingroup gfx
/// The sprites' texture array (design doc 0002 §8.7): every page of the atlas manifest as one layer
/// of an SDL_GPU 2D array texture, filled a sprite at a time as scenes load (0002.3.1 §5).

#include "core/error.hpp"
#include "core/manifest.hpp"
#include "core/types.hpp"
#include "gfx/internal/gpu.hpp"

#include <SDL3/SDL.h>

#include <expected>
#include <filesystem>
#include <vector>

namespace gfx::internal {

/// The texture array and the sampler that reads it (linear, trilinear between mips, clamped).
struct SpriteArray {
    /// One layer per atlas page, with the manifest's mip levels.
    Gpu<SDL_GPUTexture> texture = nullptr;
    /// Linear filtering with mipmaps, for smooth painted art at any zoom.
    Gpu<SDL_GPUSampler> sampler = nullptr;
    /// The sprites' distance fields (R8_UNORM, 255: far outside), with `texture`'s layers, size and mips. A sprite's field
    /// fills its region and core::FIELD_REACH_TEXELS around it (gfx::FieldBlockOf()), not its colour block (its region and
    /// the manifest's gutter); the sprite fragment shader reads it for the glows.
    Gpu<SDL_GPUTexture> field = nullptr;
    /// Linear filtering with mipmaps, clamped, for reading `field`.
    Gpu<SDL_GPUSampler> fieldSampler = nullptr;
};

/// What DecodeSprite() makes of a sprite.
struct DecodedSprite {
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The image, premultiplied.
    Surface image{};
    /// The sprite's distance field (gfx::DistanceField()): its block of the field array, rows without gaps.
    std::vector<u8> field{};
    // NOLINTEND(readability-redundant-member-init)
    /// How long computing the field took.
    u64 fieldNs = 0;
};

/// The bytes the field array of `manifest` takes on the GPU, every mip level counted.
[[nodiscard]] u64 FieldBytes(const core::Manifest& manifest);

/// Creates the texture array for every page of `manifest`, cleared to transparent, and its sampler, and the field
/// array cleared to far outside with its sampler; the clears are submitted before it returns. No sprite is in them
/// yet. Fails when the device can't make R8_UNORM both a sampled texture and a colour target (the mips need it).
[[nodiscard]] std::expected<SpriteArray, core::Error> CreateSpriteArray(SDL_GPUDevice* device, const core::Manifest& manifest);

/// Decodes `sprite`'s image from `assets`, premultiplies its alpha in linear space and computes its distance field
/// (the alpha stays as it is), on any thread. Fails when the image is missing, unreadable, or not the size the
/// manifest says (the atlas is stale: `./dev atlas`).
[[nodiscard]] std::expected<DecodedSprite, core::Error> DecodeSprite(
    const std::filesystem::path& assets,
    const core::SpriteInfo& sprite
);

/// Uploads `decoded`, `sprite` as DecodeSprite() made it, into its blocks of `array`'s colour texture (with `edge`
/// texels around it) and field texture (gfx::FieldBlockOf()), in one submit before it returns. Fails when the field
/// isn't that block's size. The mips stay stale until GenerateMips().
[[nodiscard]] std::expected<void, core::Error> UploadSprite(
    SDL_GPUDevice* device,
    const SpriteArray& array,
    const core::SpriteInfo& sprite,
    u32 edge,
    const DecodedSprite& decoded
);

/// Generates every mip of `array` from its top level, submitted before it returns.
[[nodiscard]] std::expected<void, core::Error> GenerateMips(SDL_GPUDevice* device, const SpriteArray& array);

} // namespace gfx::internal
