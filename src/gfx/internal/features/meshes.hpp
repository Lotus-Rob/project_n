#pragma once

/// @file
/// @ingroup gfx
/// The mesh path (design doc 0002.4.2 §2): textured, coloured quads the main thread places in target pixels each frame,
/// drawn from a cycled vertex buffer with a static quad index buffer. Text is its first user: each quad's page is a
/// glyph page of distance fields, which the fragment shader turns into the text, its outline and its shadow.

#include "core/error.hpp"
#include "core/types.hpp"
#include "gfx/camera.hpp"
#include "gfx/internal/gpu.hpp"
#include "gfx/mesh_vertex.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <expected>
#include <span>

namespace gfx::internal {

/// How text is drawn from its distance fields: `TextStyle` in shaders/mesh.frag.hlsl, the fragment uniform.
struct TextStyle {
    /// 1 to draw the dark outline, 0 not to.
    f32 outline = 1.0F;
    /// 1 to draw the soft shadow, 0 not to.
    f32 shadow = 1.0F;
    /// How far right the shadow falls, as a fraction of the page.
    f32 shadowOffsetU = 0.0F;
    /// How far down the shadow falls, as a fraction of the page.
    f32 shadowOffsetV = 0.0F;
};

/// A run of consecutive uploaded quads on one page: one draw.
struct MeshRun {
    /// The R8 page of distance fields its quads sample.
    SDL_GPUTexture* page = nullptr;
    /// Its first quad among the uploaded ones.
    u32 firstQuad = 0;
    /// How many quads it draws.
    u32 quads = 0;
};

/// The mesh pipeline (premultiplied over, as core::Pipeline::Normal), its sampler and its buffers.
class MeshRenderer {
public:
    /// Creates the pipeline for colour targets of `format`, a vertex buffer of MAX_MESH_QUADS and the index buffer.
    [[nodiscard]] static std::expected<MeshRenderer, core::Error> Create(SDL_GPUDevice* device, SDL_GPUTextureFormat format);

    /// Copies `vertices` (whole quads, QUAD_VERTICES each) into the vertex buffer in `copy`, cycling it, and the quad
    /// indices too the first time. Returns how many quads didn't fit (past MAX_MESH_QUADS) and aren't drawn.
    /// Fails when a transfer buffer can't be mapped; then nothing is drawn until an upload succeeds.
    [[nodiscard]] std::expected<std::size_t, core::Error> Upload(SDL_GPUCopyPass* copy, std::span<const MeshVertex> vertices);

    /// Draws `runs` of the uploaded quads in `pass`, placed by `pixelsToClip`, their fields drawn as `style` says. Quads
    /// past what fitted aren't drawn.
    void Draw(
        SDL_GPUCommandBuffer* commands,
        SDL_GPURenderPass* pass,
        std::span<const MeshRun> runs,
        const ClipTransform& pixelsToClip,
        const TextStyle& style
    ) const;

private:
    SDL_GPUDevice* m_device = nullptr;
    Gpu<SDL_GPUGraphicsPipeline> m_pipeline = nullptr;
    Gpu<SDL_GPUSampler> m_sampler = nullptr;
    Gpu<SDL_GPUBuffer> m_vertices = nullptr;
    Gpu<SDL_GPUBuffer> m_indices = nullptr;
    Gpu<SDL_GPUTransferBuffer> m_transfer = nullptr;
    bool m_indicesUploaded = false;
    u32 m_quads = 0;
};

} // namespace gfx::internal
