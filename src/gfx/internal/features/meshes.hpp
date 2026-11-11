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

#include <array>
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
    /// The ink of the outline and the shadow: linear, red, green, blue and alpha, each 0 to 1 (the colour table's
    /// core::UiColour::TextInk, see InkOf()).
    std::array<f32, 4> ink{};
};

/// `packed` (a core::UiColours value: linear, premultiplied, red in the low byte) as TextStyle::ink.
[[nodiscard]] std::array<f32, 4> InkOf(u32 packed);

/// The indices of a quad: two triangles over its QUAD_VERTICES corners, both clockwise on screen.
inline constexpr u32 QUAD_INDICES = 6;

/// A GPU buffer of `bytes` for `usage`, named `name` for graphics debuggers; null when it can't be made.
[[nodiscard]] Gpu<SDL_GPUBuffer> CreateMeshBuffer(
    SDL_GPUDevice* device,
    SDL_GPUBufferUsageFlags usage,
    u32 bytes,
    const char* name
);

/// Records copying `bytes` through `transfer` (mapped `cycle`d) into the start of `buffer` in `copy`; false when the
/// transfer buffer can't be mapped.
[[nodiscard]] bool CopyToBuffer(
    SDL_GPUDevice* device,
    SDL_GPUCopyPass* copy,
    SDL_GPUTransferBuffer* transfer,
    SDL_GPUBuffer* buffer,
    std::span<const std::byte> bytes,
    bool cycle
);

/// Records filling `indices` with QUAD_INDICES indices for each of `quads` quads, QUAD_VERTICES vertices apart, in
/// `copy`; false when its transfer buffer can't be made or mapped.
[[nodiscard]] bool UploadQuadIndices(SDL_GPUDevice* device, SDL_GPUCopyPass* copy, SDL_GPUBuffer* indices, u32 quads);

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
