#pragma once

/// @file
/// @ingroup gfx
/// The swords' trails (the phase 6 chunk 4 spec): quads the main thread places along each sword's path in world units every
/// frame (gfx/volley_draw.hpp), drawn additively from a cycled vertex buffer through the mesh path's vertex shader, fading
/// along their length and softly across it.

#include "core/error.hpp"
#include "core/types.hpp"
#include "gfx/camera.hpp"
#include "gfx/internal/gpu.hpp"
#include "gfx/volley_draw.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <expected>
#include <span>

namespace gfx::internal {

/// The ribbon pipeline (additive: ONE, ONE) and its buffers.
class RibbonRenderer {
public:
    /// Creates the pipeline for colour targets of `format`, a vertex buffer of MAX_RIBBON_QUADS and the index buffer.
    [[nodiscard]] static std::expected<RibbonRenderer, core::Error> Create(SDL_GPUDevice* device, SDL_GPUTextureFormat format);

    /// Copies `vertices` (whole quads, QUAD_VERTICES each) into the vertex buffer in `copy`, cycling it, and the quad
    /// indices too the first time. Returns how many quads didn't fit (past MAX_RIBBON_QUADS) and aren't drawn.
    /// Fails when a transfer buffer can't be mapped; then nothing is drawn until an upload succeeds.
    [[nodiscard]] std::expected<std::size_t, core::Error> Upload(SDL_GPUCopyPass* copy, std::span<const RibbonVertex> vertices);

    /// Draws the uploaded quads in `pass`, placed by `worldToClip`.
    void Draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const ClipTransform& worldToClip) const;

private:
    SDL_GPUDevice* m_device = nullptr;
    Gpu<SDL_GPUGraphicsPipeline> m_pipeline = nullptr;
    Gpu<SDL_GPUBuffer> m_vertices = nullptr;
    Gpu<SDL_GPUBuffer> m_indices = nullptr;
    Gpu<SDL_GPUTransferBuffer> m_transfer = nullptr;
    bool m_indicesUploaded = false;
    u32 m_quads = 0;
};

} // namespace gfx::internal
