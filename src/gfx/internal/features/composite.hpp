#pragma once

/// @file
/// @ingroup gfx
/// The composite (design doc 0002.4.1 §3): the scene target drawn over the whole final pass's
/// target, encoded to sRGB by its shader, so the swapchain needn't be an sRGB one.

#include "core/error.hpp"
#include "gfx/internal/gpu.hpp"

#include <SDL3/SDL.h>

#include <expected>

namespace gfx::internal {

/// The composite's pipeline and its sampler.
class Compositor {
public:
    /// Creates the pipeline for colour targets of `format`, a non-sRGB 8-bit one.
    [[nodiscard]] static std::expected<Compositor, core::Error> Create(SDL_GPUDevice* device, SDL_GPUTextureFormat format);

    /// Draws `scene`, a texture of the pass target's size, over every pixel of `pass`'s target.
    void Draw(SDL_GPURenderPass* pass, SDL_GPUTexture* scene) const;

private:
    Gpu<SDL_GPUGraphicsPipeline> m_pipeline = nullptr;
    Gpu<SDL_GPUSampler> m_sampler = nullptr;
};

} // namespace gfx::internal
