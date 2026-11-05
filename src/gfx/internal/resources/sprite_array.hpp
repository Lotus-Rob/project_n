#pragma once

/// @file
/// @ingroup gfx
/// The sprites' texture array (design doc 0002 §8.7): every page of the atlas manifest as one layer
/// of an SDL_GPU 2D array texture, filled from the sprite images at load.

#include "core/error.hpp"
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

/// Decodes every sprite of the atlas manifest from `assets` (its `sprites/` PNGs), premultiplies
/// its alpha in linear space, uploads it into its region of its page, and generates the mips. The commands are
/// submitted before it returns. Fails when an image is missing, unreadable, or not the size the
/// manifest says (the atlas is stale: `./dev atlas`).
[[nodiscard]] std::expected<SpriteArray, core::Error> LoadSpriteArray(
    SDL_GPUDevice* device,
    const std::filesystem::path& assets
);

} // namespace gfx::internal
