#include "gfx/internal/features/rivers.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "gfx/fog.hpp"
#include "gfx/generated/shaders.hpp" // by `project_n shaders`
#include "gfx/internal/features/meshes.hpp"

namespace gfx::internal {

namespace {

/// Premultiplied over, as core::Pipeline::Normal: a band covers the ground by its alpha.
constexpr SDL_GPUColorTargetBlendState OVER{ .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                                             .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                             .color_blend_op = SDL_GPU_BLENDOP_ADD,
                                             .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                                             .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                             .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                                             .color_write_mask = 0,
                                             .enable_blend = true,
                                             .enable_color_write_mask = false,
                                             .padding1 = 0,
                                             .padding2 = 0 };
/// The fragment stage's uniform slots: the fog's first, as every land shader has it, then the band's.
constexpr u32 FOG_UNIFORM_SLOT = 0;
constexpr u32 BAND_UNIFORM_SLOT = 1;
/// A core band's rim start: it never fades.
constexpr f32 SOLID_BAND = 1.0F;

/// `RiverBand` in shaders/river.frag.hlsl: where the band drawn starts fading toward its sides, then a pad to the row.
struct BandUniforms {
    f32 rimStart = SOLID_BAND;
    std::array<f32, 3> padding{};
};

} // namespace

std::expected<LineRibbonRenderer, core::Error> LineRibbonRenderer::Create(
    SDL_GPUDevice* device,
    SDL_GPUTextureFormat format,
    u32 maxQuads,
    std::string_view name
) {
    LineRibbonRenderer renderer;
    renderer.m_device = device;
    renderer.m_name = name;
    renderer.m_maxQuads = maxQuads;
    renderer.m_pipeline =
        CreateRibbonPipeline(device, format, shaders::RIVER_VERT, shaders::RIVER_FRAG, OVER, renderer.m_name.c_str());
    if (renderer.m_pipeline == nullptr) {
        return std::unexpected{ core::Error{ std::format("the {} pipeline was rejected: {}", name, SDL_GetError()) } };
    }
    std::expected<RibbonBuffers, core::Error> buffers = CreateRibbonBuffers(device, maxQuads, name);
    if (!buffers) {
        return std::unexpected{ std::move(buffers.error()) };
    }
    renderer.m_buffers = std::move(*buffers);
    return renderer;
}

std::expected<void, core::Error> LineRibbonRenderer::Upload(SDL_GPUCopyPass* copy, u64 version, f32 rimStart) {
    PN_PROFILE_ZONE();
    if (!m_indicesHeld) {
        if (!UploadQuadIndices(m_device, copy, m_buffers.indices.get(), m_maxQuads)) {
            return std::unexpected{ core::Error{
                std::format("the {} quad indices weren't uploaded: {}", m_name, SDL_GetError()) } };
        }
        m_indicesRecorded = true;
    }
    m_quads = static_cast<u32>(m_laid.size() / QUAD_VERTICES);
    PN_ASSERT(m_quads <= m_maxQuads, "the layout keeps within the buffer it was made for");
    m_rimStart = rimStart;
    // Cycled: a frame in flight may still read what the transfer buffer and the vertex buffer held.
    if (m_quads > 0 &&
        !CopyToBuffer(
            m_device, copy, m_buffers.transfer.get(), m_buffers.vertices.get(), std::as_bytes(std::span{ m_laid }), true
        )) {
        return std::unexpected{ core::Error{ std::format("the {} vertices weren't uploaded: {}", m_name, SDL_GetError()) } };
    }
    m_recorded = version;
    m_ready = true;
    return {};
}

void LineRibbonRenderer::Submitted(bool submitted) {
    if (submitted && m_recorded) {
        m_held = m_recorded;
    }
    if (submitted && m_indicesRecorded) {
        m_indicesHeld = true;
    }
    m_recorded.reset();
    m_indicesRecorded = false;
}

void LineRibbonRenderer::Draw(
    SDL_GPUCommandBuffer* commands,
    SDL_GPURenderPass* pass,
    const ClipTransform& worldToClip,
    const FogTexture& fog
) const {
    PN_PROFILE_ZONE();
    if (!m_ready || m_quads == 0) {
        return;
    }
    // The layout gives each line's two bands the same quads, every edge band's before every core's.
    PN_ASSERT(m_quads % 2 == 0, "the lines' edge and core bands take as many quads each");
    const u32 edgeQuads = m_quads / 2;
    SDL_BindGPUGraphicsPipeline(pass, m_pipeline.get());
    const SDL_GPUBufferBinding vertices{ .buffer = m_buffers.vertices.get(), .offset = 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
    const SDL_GPUBufferBinding indices{ .buffer = m_buffers.indices.get(), .offset = 0 };
    SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    const std::array<f32, 4> place{ { worldToClip.scaleX, worldToClip.scaleY, worldToClip.offsetX, worldToClip.offsetY } };
    SDL_PushGPUVertexUniformData(commands, 0, place.data(), sizeof(place));
    const SDL_GPUTextureSamplerBinding fogBinding = fog.Binding();
    SDL_BindGPUFragmentSamplers(pass, 0, &fogBinding, 1);
    SDL_PushGPUFragmentUniformData(commands, FOG_UNIFORM_SLOT, &fog.Uniforms(), sizeof(FogUniforms));
    const BandUniforms edge{ .rimStart = m_rimStart, .padding = {} };
    SDL_PushGPUFragmentUniformData(commands, BAND_UNIFORM_SLOT, &edge, sizeof(edge));
    SDL_DrawGPUIndexedPrimitives(pass, edgeQuads * QUAD_INDICES, 1, 0, 0, 0);
    const BandUniforms solid{ .rimStart = SOLID_BAND, .padding = {} };
    SDL_PushGPUFragmentUniformData(commands, BAND_UNIFORM_SLOT, &solid, sizeof(solid));
    SDL_DrawGPUIndexedPrimitives(pass, (m_quads - edgeQuads) * QUAD_INDICES, 1, edgeQuads * QUAD_INDICES, 0, 0);
}

} // namespace gfx::internal
