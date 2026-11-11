#include "gfx/internal/features/ribbons.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "gfx/internal/features/meshes.hpp"
#include "gfx/internal/resources/shaders.hpp"

namespace gfx::internal {

namespace {

constexpr u32 RIBBON_VERTEX_BYTES = MAX_RIBBON_QUADS * QUAD_VERTICES * static_cast<u32>(sizeof(RibbonVertex));
constexpr u32 RIBBON_INDEX_BYTES = MAX_RIBBON_QUADS * QUAD_INDICES * static_cast<u32>(sizeof(u32));

Gpu<SDL_GPUGraphicsPipeline> CreateRibbonPipeline(SDL_GPUDevice* device, SDL_GPUTextureFormat format) {
    const Gpu<SDL_GPUShader> vertex = CreateShader(device, shaders::MESH_VERT, SDL_GPU_SHADERSTAGE_VERTEX);
    const Gpu<SDL_GPUShader> fragment = CreateShader(device, shaders::RIBBON_FRAG, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (vertex == nullptr || fragment == nullptr) {
        return { nullptr, { device } };
    }
    SDL_GPUColorTargetDescription target{};
    target.format = format;
    target.blend_state.enable_blend = true; // additive, as core::Pipeline::Additive (§8.8)
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
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
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, "ribbons");
    // The pipeline keeps what it needs of the shaders, which are released on return.
    Gpu<SDL_GPUGraphicsPipeline> pipeline{ SDL_CreateGPUGraphicsPipeline(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return pipeline;
}

} // namespace

std::expected<RibbonRenderer, core::Error> RibbonRenderer::Create(SDL_GPUDevice* device, SDL_GPUTextureFormat format) {
    RibbonRenderer renderer;
    renderer.m_device = device;
    renderer.m_pipeline = CreateRibbonPipeline(device, format);
    if (renderer.m_pipeline == nullptr) {
        return std::unexpected{ core::Error{ std::format("the ribbon pipeline was rejected: {}", SDL_GetError()) } };
    }
    renderer.m_vertices = CreateMeshBuffer(device, SDL_GPU_BUFFERUSAGE_VERTEX, RIBBON_VERTEX_BYTES, "ribbon vertices");
    renderer.m_indices = CreateMeshBuffer(device, SDL_GPU_BUFFERUSAGE_INDEX, RIBBON_INDEX_BYTES, "ribbon quad indices");
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = RIBBON_VERTEX_BYTES;
    renderer.m_transfer = Gpu<SDL_GPUTransferBuffer>{ SDL_CreateGPUTransferBuffer(device, &transferInfo), { device } };
    if (renderer.m_vertices == nullptr || renderer.m_indices == nullptr || renderer.m_transfer == nullptr) {
        return std::unexpected{ core::Error{ std::format("the ribbon buffers weren't created: {}", SDL_GetError()) } };
    }
    return renderer;
}

std::expected<std::size_t, core::Error> RibbonRenderer::Upload(SDL_GPUCopyPass* copy, std::span<const RibbonVertex> vertices) {
    PN_PROFILE_ZONE();
    if (!m_indicesUploaded) {
        if (!UploadQuadIndices(m_device, copy, m_indices.get(), MAX_RIBBON_QUADS)) {
            return std::unexpected{ core::Error{
                std::format("the ribbon quad indices weren't uploaded: {}", SDL_GetError()) } };
        }
        m_indicesUploaded = true;
    }
    const std::size_t quads = vertices.size() / QUAD_VERTICES;
    m_quads = static_cast<u32>(std::min<std::size_t>(quads, MAX_RIBBON_QUADS));
    const std::size_t dropped = quads - m_quads;
    if (m_quads == 0) {
        return dropped;
    }
    const std::span<const RibbonVertex> fitting = vertices.first(std::size_t{ m_quads } * QUAD_VERTICES);
    // Cycled: a frame in flight may still read what the transfer buffer and the vertex buffer held (§8.7).
    if (!CopyToBuffer(m_device, copy, m_transfer.get(), m_vertices.get(), std::as_bytes(fitting), true)) {
        m_quads = 0;
        return std::unexpected{ core::Error{ std::format("the ribbon vertices weren't uploaded: {}", SDL_GetError()) } };
    }
    return dropped;
}

void RibbonRenderer::Draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const ClipTransform& worldToClip) const {
    PN_PROFILE_ZONE();
    if (m_quads == 0) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(pass, m_pipeline.get());
    const SDL_GPUBufferBinding vertices{ .buffer = m_vertices.get(), .offset = 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
    const SDL_GPUBufferBinding indices{ .buffer = m_indices.get(), .offset = 0 };
    SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    const std::array<f32, 4> uniforms{ worldToClip.scaleX, worldToClip.scaleY, worldToClip.offsetX, worldToClip.offsetY };
    SDL_PushGPUVertexUniformData(commands, 0, uniforms.data(), sizeof(uniforms));
    SDL_DrawGPUIndexedPrimitives(pass, m_quads * QUAD_INDICES, 1, 0, 0, 0);
}

} // namespace gfx::internal
