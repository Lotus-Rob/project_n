#include "gfx/internal/features/meshes.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "gfx/internal/resources/shaders.hpp"

namespace gfx::internal {

namespace {

constexpr u32 QUAD_INDICES = 6;
constexpr std::array<u32, QUAD_INDICES> QUAD_CORNERS{ { 0, 1, 2, 2, 1, 3 } }; // two triangles, both clockwise on screen
constexpr u32 VERTEX_BYTES = MAX_MESH_QUADS * QUAD_VERTICES * static_cast<u32>(MESH_VERTEX_BYTES);
constexpr u32 INDEX_BYTES = MAX_MESH_QUADS * QUAD_INDICES * static_cast<u32>(sizeof(u32));

constexpr u32 CHANNEL_MASK = 0xFFU;
constexpr u32 CHANNEL_BITS = 8;
constexpr f32 CHANNEL_MAX = 255.0F;

Gpu<SDL_GPUGraphicsPipeline> CreateMeshPipeline(SDL_GPUDevice* device, SDL_GPUTextureFormat format) {
    const Gpu<SDL_GPUShader> vertex = CreateShader(device, shaders::MESH_VERT, SDL_GPU_SHADERSTAGE_VERTEX);
    const Gpu<SDL_GPUShader> fragment = CreateShader(device, shaders::MESH_FRAG, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (vertex == nullptr || fragment == nullptr) {
        return { nullptr, { device } };
    }
    SDL_GPUColorTargetDescription target{};
    target.format = format;
    target.blend_state.enable_blend = true; // premultiplied over (§8.8)
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    const SDL_GPUVertexBufferDescription buffer{
        .slot = 0, .pitch = MESH_VERTEX_BYTES, .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX, .instance_step_rate = 0
    };
    const std::array<SDL_GPUVertexAttribute, 3> attributes{ {
        { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(MeshVertex, x) },
        { .location = 1,
          .buffer_slot = 0,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM,
          .offset = offsetof(MeshVertex, uv) },
        { .location = 2,
          .buffer_slot = 0,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
          .offset = offsetof(MeshVertex, colour) },
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
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, "meshes");
    // The pipeline keeps what it needs of the shaders, which are released on return.
    Gpu<SDL_GPUGraphicsPipeline> pipeline{ SDL_CreateGPUGraphicsPipeline(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return pipeline;
}

Gpu<SDL_GPUBuffer> CreateMeshBuffer(SDL_GPUDevice* device, SDL_GPUBufferUsageFlags usage, u32 bytes, const char* name) {
    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size = bytes;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_BUFFER_CREATE_NAME_STRING, name);
    Gpu<SDL_GPUBuffer> buffer{ SDL_CreateGPUBuffer(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return buffer;
}

// Records copying `bytes` through `transfer` (mapped `cycle`d) into the start of `buffer`; false when it can't be mapped.
bool CopyToBuffer(
    SDL_GPUDevice* device,
    SDL_GPUCopyPass* copy,
    SDL_GPUTransferBuffer* transfer,
    SDL_GPUBuffer* buffer,
    std::span<const std::byte> bytes,
    bool cycle
) {
    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, cycle);
    if (mapped == nullptr) {
        return false;
    }
    std::ranges::copy(bytes, BytesAt(mapped, bytes.size()).begin());
    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = transfer;
    SDL_GPUBufferRegion destination{};
    destination.buffer = buffer;
    destination.size = static_cast<u32>(bytes.size());
    SDL_UploadToGPUBuffer(copy, &source, &destination, cycle);
    return true;
}

} // namespace

std::array<f32, 4> InkOf(u32 packed) {
    std::array<f32, 4> ink{};
    for (std::size_t channel = 0; channel < ink.size(); ++channel) {
        ink.at(channel) = static_cast<f32>((packed >> (channel * CHANNEL_BITS)) & CHANNEL_MASK) / CHANNEL_MAX;
    }
    return ink;
}

std::expected<MeshRenderer, core::Error> MeshRenderer::Create(SDL_GPUDevice* device, SDL_GPUTextureFormat format) {
    MeshRenderer renderer;
    renderer.m_device = device;
    renderer.m_pipeline = CreateMeshPipeline(device, format);
    if (renderer.m_pipeline == nullptr) {
        return std::unexpected{ core::Error{ std::format("the mesh pipeline was rejected: {}", SDL_GetError()) } };
    }
    // Linear: a distance field interpolates into the distance between its texels, which keeps edges smooth at any scale.
    SDL_GPUSamplerCreateInfo sampler{};
    sampler.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    renderer.m_sampler = Gpu<SDL_GPUSampler>{ SDL_CreateGPUSampler(device, &sampler), { device } };
    renderer.m_vertices = CreateMeshBuffer(device, SDL_GPU_BUFFERUSAGE_VERTEX, VERTEX_BYTES, "mesh vertices");
    renderer.m_indices = CreateMeshBuffer(device, SDL_GPU_BUFFERUSAGE_INDEX, INDEX_BYTES, "mesh quad indices");
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = VERTEX_BYTES;
    renderer.m_transfer = Gpu<SDL_GPUTransferBuffer>{ SDL_CreateGPUTransferBuffer(device, &transferInfo), { device } };
    if (renderer.m_sampler == nullptr || renderer.m_vertices == nullptr || renderer.m_indices == nullptr ||
        renderer.m_transfer == nullptr) {
        return std::unexpected{ core::Error{ std::format("the mesh buffers weren't created: {}", SDL_GetError()) } };
    }
    return renderer;
}

std::expected<std::size_t, core::Error> MeshRenderer::Upload(SDL_GPUCopyPass* copy, std::span<const MeshVertex> vertices) {
    PN_PROFILE_ZONE();
    if (!m_indicesUploaded) {
        std::vector<u32> indices(static_cast<std::size_t>(MAX_MESH_QUADS) * QUAD_INDICES);
        for (std::size_t index = 0; index < indices.size(); ++index) {
            indices[index] = static_cast<u32>(((index / QUAD_INDICES) * QUAD_VERTICES) + QUAD_CORNERS.at(index % QUAD_INDICES));
        }
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = INDEX_BYTES;
        // Released once recorded: SDL keeps it until the copy has run.
        const Gpu<SDL_GPUTransferBuffer> transfer{ SDL_CreateGPUTransferBuffer(m_device, &info), { m_device } };
        if (transfer == nullptr ||
            !CopyToBuffer(m_device, copy, transfer.get(), m_indices.get(), std::as_bytes(std::span{ indices }), false)) {
            return std::unexpected{ core::Error{ std::format("the mesh quad indices weren't uploaded: {}", SDL_GetError()) } };
        }
        m_indicesUploaded = true;
    }
    const std::size_t dropped = MeshQuadsDropped(vertices.size());
    m_quads = static_cast<u32>((vertices.size() / QUAD_VERTICES) - dropped);
    const std::size_t fitting = std::size_t{ m_quads } * QUAD_VERTICES;
    if (fitting == 0) {
        return dropped;
    }
    // Cycled: a frame in flight may still read what the transfer buffer and the vertex buffer held (§8.7).
    if (!CopyToBuffer(m_device, copy, m_transfer.get(), m_vertices.get(), std::as_bytes(vertices.first(fitting)), true)) {
        m_quads = 0;
        return std::unexpected{ core::Error{ std::format("the mesh vertices weren't uploaded: {}", SDL_GetError()) } };
    }
    return dropped;
}

void MeshRenderer::Draw(
    SDL_GPUCommandBuffer* commands,
    SDL_GPURenderPass* pass,
    std::span<const MeshRun> runs,
    const ClipTransform& pixelsToClip,
    const TextStyle& style
) const {
    PN_PROFILE_ZONE();
    if (m_quads == 0) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(pass, m_pipeline.get());
    const SDL_GPUBufferBinding vertices{ .buffer = m_vertices.get(), .offset = 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
    const SDL_GPUBufferBinding indices{ .buffer = m_indices.get(), .offset = 0 };
    SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    const std::array<f32, 4> uniforms{ pixelsToClip.scaleX, pixelsToClip.scaleY, pixelsToClip.offsetX, pixelsToClip.offsetY };
    SDL_PushGPUVertexUniformData(commands, 0, uniforms.data(), sizeof(uniforms));
    SDL_PushGPUFragmentUniformData(commands, 0, &style, sizeof(style));
    for (const MeshRun& run : runs) {
        if (run.firstQuad >= m_quads) {
            break;
        }
        const SDL_GPUTextureSamplerBinding page{ .texture = run.page, .sampler = m_sampler.get() };
        SDL_BindGPUFragmentSamplers(pass, 0, &page, 1);
        const u32 quads = std::min(run.quads, m_quads - run.firstQuad);
        SDL_DrawGPUIndexedPrimitives(pass, quads * QUAD_INDICES, 1, run.firstQuad * QUAD_INDICES, 0, 0);
    }
}

} // namespace gfx::internal
