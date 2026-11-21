#pragma once

/// @file
/// @ingroup gfx
/// The swords' trails: quads the main thread places along each sword's path in world units every
/// frame (gfx/volley_draw.hpp), drawn additively from a cycled vertex buffer through the mesh path's vertex shader, fading
/// along their length and softly across it. Also what every pipeline drawing gfx::RibbonVertex quads shares (the trails,
/// the world map's rivers): its pipeline's vertex layout and its buffers.

#include "core/error.hpp"
#include "core/types.hpp"
#include "gfx/camera.hpp"
#include "gfx/internal/gpu.hpp"
#include "gfx/internal/resources/shaders.hpp"
#include "gfx/volley_draw.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <expected>
#include <span>
#include <string_view>

namespace gfx::internal {

/// A pipeline drawing whole quads of RibbonVertex as indexed triangle lists from one vertex buffer, `vertex` reading each
/// vertex's position, unorm16 UV and unorm8 colour at locations 0, 1 and 2, `fragment` shading it, into colour targets of
/// `format` blended by `blend`, named `name` for graphics debuggers; null, with SDL's error set, when a shader or the
/// pipeline is rejected.
[[nodiscard]] Gpu<SDL_GPUGraphicsPipeline> CreateRibbonPipeline(
    SDL_GPUDevice* device,
    SDL_GPUTextureFormat format,
    const shaders::Stage& vertex,
    const shaders::Stage& fragment,
    const SDL_GPUColorTargetBlendState& blend,
    const char* name
);

/// What a ribbon pipeline draws from.
struct RibbonBuffers {
    /// Room for the quads' vertices.
    Gpu<SDL_GPUBuffer> vertices = nullptr;
    /// Room for the quads' indices, filled by UploadQuadIndices().
    Gpu<SDL_GPUBuffer> indices = nullptr;
    /// The upload buffer, the vertices' size.
    Gpu<SDL_GPUTransferBuffer> transfer = nullptr;
};

/// The buffers for `quads` quads of RibbonVertex, named "`name` vertices" and "`name` quad indices" for graphics debuggers,
/// or the reason one couldn't be made, naming `name`.
[[nodiscard]] std::expected<RibbonBuffers, core::Error> CreateRibbonBuffers(
    SDL_GPUDevice* device,
    u32 quads,
    std::string_view name
);

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
    RibbonBuffers m_buffers{};
    bool m_indicesUploaded = false;
    u32 m_quads = 0;
};

} // namespace gfx::internal
