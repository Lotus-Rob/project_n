#include "gfx/internal/features/sprites.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "gfx/internal/resources/shaders.hpp"

namespace gfx::internal {

namespace {

constexpr u32 VERTICES_PER_QUAD = 4; // a triangle strip

// `Draw` in shaders/sprite.vert.hlsl: a float4, then a float, two uints and a float.
constexpr std::size_t DRAW_UNIFORM_BYTES = 32;
struct DrawUniforms {
    std::array<f32, 4> worldToClip{};
    f32 alpha = 0.0F;
    u32 firstInstance = 0;
    u32 outlineColour = 0; // 0: an ordinary sprite
    f32 ring = 0.0F;       // the outline's width in UV; 0 for an ordinary sprite
};
static_assert(sizeof(DrawUniforms) == DRAW_UNIFORM_BYTES, "the shader's constant buffer is 32 bytes");

Gpu<SDL_GPUGraphicsPipeline> CreatePipeline(SDL_GPUDevice* device, SDL_GPUTextureFormat format, core::Pipeline blend) {
    const Gpu<SDL_GPUShader> vertex = CreateShader(device, shaders::SPRITE_VERT, SDL_GPU_SHADERSTAGE_VERTEX);
    const Gpu<SDL_GPUShader> fragment = CreateShader(device, shaders::SPRITE_FRAG, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (vertex == nullptr || fragment == nullptr) {
        return { nullptr, { device } };
    }
    SDL_GPUColorTargetDescription target{};
    target.format = format;
    // Premultiplied alpha's blends (§8.8): normal is ONE, ONE_MINUS_SRC_ALPHA; additive is ONE, ONE.
    const SDL_GPUBlendFactor destination =
        blend == core::Pipeline::Additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_color_blendfactor = destination;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = destination;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex.get();
    info.fragment_shader = fragment.get();
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(
        info.props,
        SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING,
        blend == core::Pipeline::Additive ? "sprites, additive" : "sprites"
    );
    // The pipeline keeps what it needs of the shaders, which are released on return.
    Gpu<SDL_GPUGraphicsPipeline> pipeline{ SDL_CreateGPUGraphicsPipeline(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return pipeline;
}

} // namespace

std::expected<SpriteRenderer, core::Error> SpriteRenderer::Create(
    SDL_GPUDevice* device,
    SDL_GPUTextureFormat format,
    u32 capacity,
    const char* name
) {
    SpriteRenderer renderer;
    renderer.m_device = device;
    renderer.m_capacity = capacity;
    const u32 bytes = capacity * static_cast<u32>(core::INSTANCE_BYTES);
    for (const core::Pipeline blend : { core::Pipeline::Normal, core::Pipeline::Additive }) {
        Gpu<SDL_GPUGraphicsPipeline>& pipeline = renderer.m_pipelines.at(static_cast<std::size_t>(blend));
        pipeline = CreatePipeline(device, format, blend);
        if (pipeline == nullptr) {
            return std::unexpected{ core::Error{ std::format("a sprite pipeline was rejected: {}", SDL_GetError()) } };
        }
    }
    SDL_GPUBufferCreateInfo bufferInfo{};
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bufferInfo.size = bytes;
    bufferInfo.props = SDL_CreateProperties();
    SDL_SetStringProperty(bufferInfo.props, SDL_PROP_GPU_BUFFER_CREATE_NAME_STRING, name);
    renderer.m_instances = Gpu<SDL_GPUBuffer>{ SDL_CreateGPUBuffer(device, &bufferInfo), { device } };
    SDL_DestroyProperties(bufferInfo.props);
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = bytes;
    renderer.m_transfer = Gpu<SDL_GPUTransferBuffer>{ SDL_CreateGPUTransferBuffer(device, &transferInfo), { device } };
    if (renderer.m_instances == nullptr || renderer.m_transfer == nullptr) {
        return std::unexpected{ core::Error{ std::format("the sprite instance buffers weren't created: {}", SDL_GetError()) } };
    }
    return renderer;
}

std::expected<std::size_t, core::Error> SpriteRenderer::Upload(
    SDL_GPUCopyPass* copy,
    std::span<const core::Instance> instances
) {
    PN_PROFILE_ZONE();
    const std::span<const core::Instance> fitting = instances.first(std::min<std::size_t>(instances.size(), m_capacity));
    m_count = static_cast<u32>(fitting.size());
    if (fitting.empty()) {
        return instances.size();
    }
    const std::span<const std::byte> bytes = std::as_bytes(fitting);
    // Cycled: a frame in flight may still read what the transfer buffer and the storage buffer held (§8.7).
    void* mapped = SDL_MapGPUTransferBuffer(m_device, m_transfer.get(), true);
    if (mapped == nullptr) {
        m_count = 0;
        return std::unexpected{ core::Error{ std::format("the sprite instances weren't uploaded: {}", SDL_GetError()) } };
    }
    std::ranges::copy(bytes, BytesAt(mapped, bytes.size()).begin());
    SDL_UnmapGPUTransferBuffer(m_device, m_transfer.get());
    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = m_transfer.get();
    SDL_GPUBufferRegion destination{};
    destination.buffer = m_instances.get();
    destination.size = static_cast<u32>(bytes.size());
    SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    return instances.size() - fitting.size();
}

void SpriteRenderer::Draw(
    SDL_GPUCommandBuffer* commands,
    SDL_GPURenderPass* pass,
    std::span<const core::Batch> batches,
    const SpriteArray& sprites,
    const ClipTransform& transform,
    f32 alpha
) const {
    PN_PROFILE_ZONE();
    if (m_count == 0) {
        return;
    }
    const std::array<SDL_GPUBuffer*, 1> storage{ m_instances.get() };
    SDL_BindGPUVertexStorageBuffers(pass, 0, storage.data(), static_cast<u32>(storage.size()));
    SDL_GPUTextureSamplerBinding atlas{};
    atlas.texture = sprites.texture.get();
    atlas.sampler = sprites.sampler.get();
    SDL_BindGPUFragmentSamplers(pass, 0, &atlas, 1);
    DrawUniforms uniforms{
        .worldToClip = { transform.scaleX, transform.scaleY, transform.offsetX, transform.offsetY },
        .alpha = alpha,
    };
    for (const core::Batch& batch : batches) {
        if (batch.first >= m_count) {
            continue; // batches may come in any order of their instances (the overlay's do)
        }
        // Bindings outlive a pipeline change within the pass, so only the pipeline is rebound.
        SDL_BindGPUGraphicsPipeline(pass, m_pipelines.at(static_cast<std::size_t>(batch.pipeline)).get());
        uniforms.firstInstance = batch.first;
        SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));
        // first_vertex and first_instance stay 0: SDL warns they don't reach SV_VertexID and SV_InstanceID (§8.5).
        SDL_DrawGPUPrimitives(pass, VERTICES_PER_QUAD, std::min(batch.count, m_count - batch.first), 0, 0);
    }
}

void SpriteRenderer::DrawOutline(
    SDL_GPUCommandBuffer* commands,
    SDL_GPURenderPass* pass,
    const Outline& outline,
    const SpriteArray& sprites,
    const ClipTransform& transform,
    f32 alpha
) const {
    PN_PROFILE_ZONE();
    if (outline.instance >= m_count) {
        return;
    }
    const std::array<SDL_GPUBuffer*, 1> storage{ m_instances.get() };
    SDL_BindGPUVertexStorageBuffers(pass, 0, storage.data(), static_cast<u32>(storage.size()));
    SDL_GPUTextureSamplerBinding atlas{};
    atlas.texture = sprites.texture.get();
    atlas.sampler = sprites.sampler.get();
    SDL_BindGPUFragmentSamplers(pass, 0, &atlas, 1);
    SDL_BindGPUGraphicsPipeline(pass, m_pipelines.at(static_cast<std::size_t>(core::Pipeline::Normal)).get());
    const DrawUniforms uniforms{
        .worldToClip = { transform.scaleX, transform.scaleY, transform.offsetX, transform.offsetY },
        .alpha = alpha,
        .firstInstance = outline.instance,
        .outlineColour = outline.colour,
        .ring = outline.ring,
    };
    SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));
    SDL_DrawGPUPrimitives(pass, VERTICES_PER_QUAD, 1, 0, 0);
}

} // namespace gfx::internal
