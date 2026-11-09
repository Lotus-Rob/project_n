#pragma once

/// @file
/// @ingroup gfx
/// The scene target (design doc 0002.4.1 §2): the half-float texture the world pass draws into
/// and the composite reads, sized to the frame it's drawn for.

#include "core/error.hpp"
#include "core/types.hpp"
#include "gfx/internal/gpu.hpp"

#include <SDL3/SDL.h>

#include <expected>

namespace gfx::internal {

/// The scene target's format: room above 1.0 for stacked additive light (0002 §8.4).
inline constexpr SDL_GPUTextureFormat SCENE_FORMAT = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

/// A colour target the composite samples, and its size in pixels.
struct SceneTarget {
    /// The texture; null until fitted.
    Gpu<SDL_GPUTexture> texture = nullptr;
    /// Its width in pixels.
    u32 width = 0;
    /// Its height in pixels.
    u32 height = 0;
};

/// Makes `target` `width` by `height` pixels, recreating its texture only when its size differs.
/// Fails, leaving it empty, when the texture can't be created.
[[nodiscard]] std::expected<void, core::Error> FitSceneTarget(
    SDL_GPUDevice* device,
    SceneTarget& target,
    u32 width,
    u32 height
);

} // namespace gfx::internal
