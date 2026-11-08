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

namespace gfx::internal {

/// The texture array and the sampler that reads it (linear, trilinear between mips, clamped).
struct SpriteArray {
    /// One layer per atlas page, with the manifest's mip levels.
    Gpu<SDL_GPUTexture> texture = nullptr;
    /// Linear filtering with mipmaps, for smooth painted art at any zoom.
    Gpu<SDL_GPUSampler> sampler = nullptr;
};

/// Creates the texture array for every page of `manifest`, cleared to transparent, and its sampler; the clears are
/// submitted before it returns. No sprite is in it yet.
[[nodiscard]] std::expected<SpriteArray, core::Error> CreateSpriteArray(SDL_GPUDevice* device, const core::Manifest& manifest);

/// Decodes `sprite`'s image from `assets` and premultiplies its alpha in linear space, on any thread. Fails when the
/// image is missing, unreadable, or not the size the manifest says (the atlas is stale: `./dev atlas`).
[[nodiscard]] std::expected<Surface, core::Error> DecodeSprite(
    const std::filesystem::path& assets,
    const core::SpriteInfo& sprite
);

/// Uploads `image`, `sprite` as DecodeSprite() made it, into its block of `array` (with its gutters, for
/// `mipLevels` mips), submitted before it returns. The mips stay stale until GenerateMips().
[[nodiscard]] std::expected<void, core::Error> UploadSprite(
    SDL_GPUDevice* device,
    const SpriteArray& array,
    const core::SpriteInfo& sprite,
    u32 mipLevels,
    const SDL_Surface& image
);

/// Generates every mip of `array` from its top level, submitted before it returns.
[[nodiscard]] std::expected<void, core::Error> GenerateMips(SDL_GPUDevice* device, const SpriteArray& array);

} // namespace gfx::internal
