#pragma once

/// @file
/// @ingroup gfx
/// The mesh path (design doc 0002.4.2 §2): textured, coloured quads the main thread places in target pixels each frame,
/// drawn from a cycled vertex buffer with a static quad index buffer. Text is its first user.

#include "core/error.hpp"
#include "core/types.hpp"
#include "gfx/camera.hpp"
#include "gfx/internal/gpu.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <expected>
#include <span>

namespace gfx::internal {

/// One corner of a quad as the vertex shader reads it (`VertexIn` in shaders/mesh.vert.hlsl): 16 bytes.
struct MeshVertex {
    /// Its x in target pixels, from the left.
    f32 x = 0.0F;
    /// Its y in target pixels, from the top.
    f32 y = 0.0F;
    /// Its place on the page as fractions of the page: u, then v, unorm16.
    u32 uv = 0;
    /// Premultiplied colour the page's coverage is multiplied by: red in the low byte, then green, blue and alpha.
    u32 colour = 0;
};
/// The bytes of one MeshVertex, which the pipeline's vertex attributes read with the same layout.
inline constexpr std::size_t MESH_VERTEX_BYTES = 16;
static_assert(sizeof(MeshVertex) == MESH_VERTEX_BYTES, "the pipeline reads mesh vertices as 16 bytes");

/// The corners of a quad, in the order its indices name them: top left, top right, bottom left, bottom right.
inline constexpr u32 QUAD_VERTICES = 4;
/// The most vertices the vertex buffer holds: 16,384 quads, all a u16 index can name.
inline constexpr u32 MAX_MESH_VERTICES = 1U << 16U;

/// A run of consecutive uploaded quads on one page: one draw.
struct MeshRun {
    /// The R8 page its quads sample.
    SDL_GPUTexture* page = nullptr;
    /// Its first quad among the uploaded ones.
    u32 firstQuad = 0;
    /// How many quads it draws.
    u32 quads = 0;
};

/// The mesh pipeline (premultiplied over, as core::Pipeline::Normal), its sampler and its buffers.
class MeshRenderer {
public:
    /// Creates the pipeline for colour targets of `format`, a vertex buffer of MAX_MESH_VERTICES and the index buffer.
    [[nodiscard]] static std::expected<MeshRenderer, core::Error> Create(SDL_GPUDevice* device, SDL_GPUTextureFormat format);

    /// Copies `vertices` (whole quads, QUAD_VERTICES each) into the vertex buffer in `copy`, cycling it, and the quad
    /// indices too the first time. Returns how many vertices didn't fit (past MAX_MESH_VERTICES) and aren't drawn.
    /// Fails when a transfer buffer can't be mapped; then nothing is drawn until an upload succeeds.
    [[nodiscard]] std::expected<std::size_t, core::Error> Upload(SDL_GPUCopyPass* copy, std::span<const MeshVertex> vertices);

    /// Draws `runs` of the uploaded quads in `pass`, placed by `pixelsToClip`. Quads past what fitted aren't drawn.
    void Draw(
        SDL_GPUCommandBuffer* commands,
        SDL_GPURenderPass* pass,
        std::span<const MeshRun> runs,
        const ClipTransform& pixelsToClip
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
