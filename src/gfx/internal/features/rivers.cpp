#include "gfx/internal/features/rivers.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "gfx/fog.hpp"
#include "gfx/ground.hpp"
#include "gfx/internal/features/meshes.hpp"
#include "gfx/internal/resources/shaders.hpp"
#include "gfx/river_draw.hpp"

namespace gfx::internal {

namespace {

constexpr u32 RIVER_VERTEX_BYTES = MAX_RIVER_QUADS * QUAD_VERTICES * static_cast<u32>(sizeof(RibbonVertex));
constexpr u32 RIVER_INDEX_BYTES = MAX_RIVER_QUADS * QUAD_INDICES * static_cast<u32>(sizeof(u32));

Gpu<SDL_GPUGraphicsPipeline> CreateRiverPipeline(SDL_GPUDevice* device, SDL_GPUTextureFormat format) {
    const Gpu<SDL_GPUShader> vertex = CreateShader(device, shaders::RIVER_VERT, SDL_GPU_SHADERSTAGE_VERTEX);
    const Gpu<SDL_GPUShader> fragment = CreateShader(device, shaders::RIVER_FRAG, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (vertex == nullptr || fragment == nullptr) {
        return { nullptr, { device } };
    }
    SDL_GPUColorTargetDescription target{};
    target.format = format;
    target.blend_state.enable_blend = true; // premultiplied over, as core::Pipeline::Normal
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    const SDL_GPUVertexBufferDescription buffer{
        .slot = 0, .pitch = sizeof(RibbonVertex), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX, .instance_step_rate = 0
    };
    const std::array<SDL_GPUVertexAttribute, 3> attributes{ {
        { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(RibbonVertex, x) },
        { .location = 1,
          .buffer_slot = 0,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM,
          .offset = offsetof(RibbonVertex, uv) },
        { .location = 2,
          .buffer_slot = 0,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
          .offset = offsetof(RibbonVertex, colour) },
    } };
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex.get();
    info.fragment_shader = fragment.get();
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes.data();
    info.vertex_input_state.num_vertex_attributes = static_cast<u32>(attributes.size());
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, "rivers");
    // The pipeline keeps what it needs of the shaders, which are released on return.
    Gpu<SDL_GPUGraphicsPipeline> pipeline{ SDL_CreateGPUGraphicsPipeline(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return pipeline;
}

} // namespace

std::expected<RiverRenderer, core::Error> RiverRenderer::Create(SDL_GPUDevice* device, SDL_GPUTextureFormat format) {
    RiverRenderer renderer;
    renderer.m_device = device;
    renderer.m_pipeline = CreateRiverPipeline(device, format);
    if (renderer.m_pipeline == nullptr) {
        return std::unexpected{ core::Error{ std::format("the river pipeline was rejected: {}", SDL_GetError()) } };
    }
    renderer.m_vertices = CreateMeshBuffer(device, SDL_GPU_BUFFERUSAGE_VERTEX, RIVER_VERTEX_BYTES, "river vertices");
    renderer.m_indices = CreateMeshBuffer(device, SDL_GPU_BUFFERUSAGE_INDEX, RIVER_INDEX_BYTES, "river quad indices");
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = RIVER_VERTEX_BYTES;
    renderer.m_transfer = Gpu<SDL_GPUTransferBuffer>{ SDL_CreateGPUTransferBuffer(device, &transferInfo), { device } };
    if (renderer.m_vertices == nullptr || renderer.m_indices == nullptr || renderer.m_transfer == nullptr) {
        return std::unexpected{ core::Error{ std::format("the river buffers weren't created: {}", SDL_GetError()) } };
    }
    return renderer;
}

std::expected<void, core::Error> RiverRenderer::Update(
    SDL_GPUCopyPass* copy,
    const core::WorldMap& map,
    const core::WorldLook& look,
    u64 version
) {
    PN_PROFILE_ZONE();
    m_ready = m_held == version;
    if (m_ready) {
        return {};
    }
    if (!m_indicesHeld) {
        if (!UploadQuadIndices(m_device, copy, m_indices.get(), MAX_RIVER_QUADS)) {
            return std::unexpected{ core::Error{ std::format("the river quad indices weren't uploaded: {}", SDL_GetError()) } };
        }
        m_indicesRecorded = true;
    }
    // DrawRivers() leaves out whole the rivers past MAX_RIVER_QUADS, and logs it.
    DrawRivers(map, RiverShapeOf(map, look.river), m_laid);
    m_quads = static_cast<u32>(m_laid.size() / QUAD_VERTICES);
    // Cycled: a frame in flight may still read what the transfer buffer and the vertex buffer held.
    if (m_quads > 0 &&
        !CopyToBuffer(m_device, copy, m_transfer.get(), m_vertices.get(), std::as_bytes(std::span{ m_laid }), true)) {
        return std::unexpected{ core::Error{ std::format("the river vertices weren't uploaded: {}", SDL_GetError()) } };
    }
    m_recorded = version;
    m_ready = true;
    return {};
}

void RiverRenderer::Submitted(bool submitted) {
    if (submitted && m_recorded) {
        m_held = m_recorded;
    }
    if (submitted && m_indicesRecorded) {
        m_indicesHeld = true;
    }
    m_recorded.reset();
    m_indicesRecorded = false;
}

void RiverRenderer::Draw(
    SDL_GPUCommandBuffer* commands,
    SDL_GPURenderPass* pass,
    const ClipTransform& worldToClip,
    const FogTexture& fog
) const {
    PN_PROFILE_ZONE();
    if (!m_ready || m_quads == 0) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(pass, m_pipeline.get());
    const SDL_GPUBufferBinding vertices{ .buffer = m_vertices.get(), .offset = 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
    const SDL_GPUBufferBinding indices{ .buffer = m_indices.get(), .offset = 0 };
    SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    const std::array<f32, 4> place{ { worldToClip.scaleX, worldToClip.scaleY, worldToClip.offsetX, worldToClip.offsetY } };
    SDL_PushGPUVertexUniformData(commands, 0, place.data(), sizeof(place));
    const SDL_GPUTextureSamplerBinding fogBinding = fog.Binding();
    SDL_BindGPUFragmentSamplers(pass, 0, &fogBinding, 1);
    SDL_PushGPUFragmentUniformData(commands, 0, &fog.Uniforms(), sizeof(FogUniforms));
    SDL_DrawGPUIndexedPrimitives(pass, m_quads * QUAD_INDICES, 1, 0, 0, 0);
}

} // namespace gfx::internal
