#include "gfx/internal/features/sprites.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "gfx/internal/resources/shaders.hpp"

namespace gfx::internal {

namespace {

constexpr u32 VERTICES_PER_QUAD = 4; // a triangle strip

/// `Draw` in shaders/sprite.vert.hlsl: rows of 16 bytes, as HLSL packs a constant buffer.
constexpr std::size_t DRAW_UNIFORM_BYTES = 80;
struct DrawUniforms {
    std::array<f32, 4> worldToClip{};
    f32 alpha = 0.0F;
    u32 firstInstance = 0;
    u32 glowOnlyRole = 0; // a glow-only draw's core::GlowRole; 0: an ordinary sprite
    f32 padding0 = 0.0F;  // to the next row
    u32 glowTick = 0;
    f32 glowFraction = 0.0F;
    f32 zoom = 1.0F;
    u32 reduceFlashing = 0;
    std::array<u32, core::HIT_KINDS> elementColours{};
    f32 texelUv = 0.0F;
    std::array<f32, 3> padding{};
};
static_assert(sizeof(DrawUniforms) == DRAW_UNIFORM_BYTES, "the shader's constant buffer is 80 bytes");
/// The constants the sprite shaders copy, as they hold them: each must equal what it copies.
constexpr std::size_t SHADER_GLOW_ROLES = 8;       // sprite.vert.hlsl's GLOW_ROLES
constexpr u8 SHADER_GLOW_BLEND_INK = 1;            // sprite.vert.hlsl's GLOW_BLEND_INK
constexpr std::size_t SHADER_ELEMENTS = 4;         // sprite.vert.hlsl's LAST_ELEMENT + 1, and elementColours' uint4
constexpr u32 SHADER_FIELD_REACH_TEXELS = 16;      // sprite.hlsli's FIELD_REACH_TEXELS
constexpr u8 SHADER_DRIVE_PULSE = 1;               // sprite.vert.hlsl's DRIVE_PULSE
constexpr u8 SHADER_DRIVE_CHARGE = 2;              // sprite.vert.hlsl's DRIVE_CHARGE
constexpr u8 SHADER_DRIVE_PROGRESS = 3;            // sprite.vert.hlsl's DRIVE_PROGRESS
constexpr u8 SHADER_CURVE_EASE_IN = 1;             // sprite.vert.hlsl's CURVE_EASE_IN
constexpr u8 SHADER_CURVE_EASE_OUT = 2;            // sprite.vert.hlsl's CURVE_EASE_OUT
constexpr u8 SHADER_CURVE_SMOOTH = 3;              // sprite.vert.hlsl's CURVE_SMOOTH
constexpr u8 SHADER_CURVE_SINE = 4;                // sprite.vert.hlsl's CURVE_SINE
constexpr f32 SHADER_REDUCED_LIGHT_OPACITY = 0.5F; // sprite.vert.hlsl's REDUCED_LIGHT_OPACITY
static_assert(core::GLOW_ROLES == SHADER_GLOW_ROLES, "sprite.vert.hlsl's GLOW_ROLES is the glow table's length");
static_assert(std::to_underlying(core::GlowBlend::Ink) == SHADER_GLOW_BLEND_INK, "sprite.vert.hlsl's GLOW_BLEND_INK is Ink");
static_assert(core::HIT_KINDS == SHADER_ELEMENTS, "sprite.vert.hlsl's LAST_ELEMENT and elementColours hold every HitKind");
static_assert(core::FIELD_REACH_TEXELS == SHADER_FIELD_REACH_TEXELS, "sprite.hlsli's FIELD_REACH_TEXELS is the fields' reach");
static_assert(
    std::to_underlying(core::DriveKind::Steady) == 0 && std::to_underlying(core::DriveKind::Pulse) == SHADER_DRIVE_PULSE &&
        std::to_underlying(core::DriveKind::Charge) == SHADER_DRIVE_CHARGE &&
        std::to_underlying(core::DriveKind::Progress) == SHADER_DRIVE_PROGRESS,
    "sprite.vert.hlsl's DRIVE_ constants are core::DriveKind's values"
);
static_assert(
    std::to_underlying(core::GlowCurve::Linear) == 0 && std::to_underlying(core::GlowCurve::EaseIn) == SHADER_CURVE_EASE_IN &&
        std::to_underlying(core::GlowCurve::EaseOut) == SHADER_CURVE_EASE_OUT &&
        std::to_underlying(core::GlowCurve::Smooth) == SHADER_CURVE_SMOOTH &&
        std::to_underlying(core::GlowCurve::Sine) == SHADER_CURVE_SINE,
    "sprite.vert.hlsl's CURVE_ constants are core::GlowCurve's values"
);
static_assert(
    std::bit_cast<u32>(core::REDUCED_LIGHT_OPACITY) == std::bit_cast<u32>(SHADER_REDUCED_LIGHT_OPACITY),
    "sprite.vert.hlsl's REDUCED_LIGHT_OPACITY is core's"
);

constexpr u32 GLOW_TABLE_BYTES = static_cast<u32>(GLOW_RECORD_BYTES * core::GLOW_ROLES);

constexpr u32 SHADER_FOGGED_LAND = 1; // fog.hlsli's INSTANCE_FOGGED_LAND
constexpr u32 SHADER_SIGHT_FADE = 2;  // fog.hlsli's INSTANCE_SIGHT_FADE
constexpr u32 SHADER_FOG_SHADE = 4;   // fog.hlsli's INSTANCE_FOG_SHADE
static_assert(core::INSTANCE_FOGGED_LAND == SHADER_FOGGED_LAND, "fog.hlsli's INSTANCE_FOGGED_LAND is core's");
static_assert(core::INSTANCE_SIGHT_FADE == SHADER_SIGHT_FADE, "fog.hlsli's INSTANCE_SIGHT_FADE is core's");
static_assert(core::INSTANCE_FOG_SHADE == SHADER_FOG_SHADE, "fog.hlsli's INSTANCE_FOG_SHADE is core's");
/// Two bytes a texel: explored, then in sight, as FogFrame::values holds them.
constexpr u32 FOG_TEXEL_BYTES = 2;
constexpr u8 FOG_FULL = 255;
Gpu<SDL_GPUTexture> CreateFogTexture(SDL_GPUDevice* device, u32 columns, u32 rows, const char* name) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    // UNORM, not sRGB: the shader must read a byte as FogSample() does, divided by 255.
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = columns;
    info.height = rows;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, name);
    Gpu<SDL_GPUTexture> texture{ SDL_CreateGPUTexture(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return texture;
}

Gpu<SDL_GPUTransferBuffer> CreateTransfer(SDL_GPUDevice* device, u32 bytes) {
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = bytes;
    return Gpu<SDL_GPUTransferBuffer>{ SDL_CreateGPUTransferBuffer(device, &info), { device } };
}

/// Copies `bytes` through `transfer` into the whole of `texture`, `columns` × `rows` texels, in `copy`, both cycled, so
/// frames in flight keep what they read. False when the transfer buffer can't be mapped.
bool CopyFog(
    SDL_GPUDevice* device,
    SDL_GPUCopyPass* copy,
    SDL_GPUTransferBuffer* transfer,
    SDL_GPUTexture* texture,
    std::span<const u8> bytes,
    u32 columns,
    u32 rows
) {
    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, true);
    if (mapped == nullptr) {
        return false;
    }
    std::ranges::copy(std::as_bytes(bytes), BytesAt(mapped, bytes.size()).begin());
    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = columns;
    source.rows_per_layer = rows;
    SDL_GPUTextureRegion region{};
    region.texture = texture;
    region.w = columns;
    region.h = rows;
    region.d = 1;
    SDL_UploadToGPUTexture(copy, &source, &region, true);
    return true;
}

/// What every draw of a frame pushes, before its batch's first instance.
DrawUniforms UniformsOf(const ClipTransform& transform, f32 alpha, const GlowFrame& glows) {
    return { .worldToClip = { transform.scaleX, transform.scaleY, transform.offsetX, transform.offsetY },
             .alpha = alpha,
             .glowTick = glows.tick,
             .glowFraction = glows.fraction,
             .zoom = glows.zoom,
             .reduceFlashing = glows.reduceFlashing ? 1U : 0U,
             .elementColours = glows.elementColours,
             .texelUv = glows.texelUv };
}

/// Binds the vertex stage's instances and glow table, and the fragment stage's atlas, distance fields and fog, and pushes
/// the fog's uniforms.
void Bind(
    SDL_GPUCommandBuffer* commands,
    SDL_GPURenderPass* pass,
    SDL_GPUBuffer* instances,
    const SpriteArray& sprites,
    const GlowFrame& glows,
    const FogTexture& fog
) {
    const std::array<SDL_GPUBuffer*, 2> storage{ instances, glows.table };
    SDL_BindGPUVertexStorageBuffers(pass, 0, storage.data(), static_cast<u32>(storage.size()));
    const std::array<SDL_GPUTextureSamplerBinding, 3> textures{ {
        { .texture = sprites.texture.get(), .sampler = sprites.sampler.get() },
        { .texture = sprites.field.get(), .sampler = sprites.fieldSampler.get() },
        fog.Binding(),
    } };
    SDL_BindGPUFragmentSamplers(pass, 0, textures.data(), static_cast<u32>(textures.size()));
    SDL_PushGPUFragmentUniformData(commands, 0, &fog.Uniforms(), sizeof(FogUniforms));
}

Gpu<SDL_GPUGraphicsPipeline> CreatePipeline(SDL_GPUDevice* device, SDL_GPUTextureFormat format, core::Pipeline blend) {
    const Gpu<SDL_GPUShader> vertex = CreateShader(device, shaders::SPRITE_VERT, SDL_GPU_SHADERSTAGE_VERTEX);
    const Gpu<SDL_GPUShader> fragment = CreateShader(device, shaders::SPRITE_FRAG, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (vertex == nullptr || fragment == nullptr) {
        return { nullptr, { device } };
    }
    SDL_GPUColorTargetDescription target{};
    target.format = format;
    // Premultiplied alpha's blends: normal is ONE, ONE_MINUS_SRC_ALPHA; additive is ONE, ONE.
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

std::expected<GlowTableBuffer, core::Error> GlowTableBuffer::Create(
    SDL_GPUDevice* device,
    const std::array<GlowRecord, core::GLOW_ROLES>& table
) {
    GlowTableBuffer buffer;
    buffer.m_device = device;
    SDL_GPUBufferCreateInfo bufferInfo{};
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bufferInfo.size = GLOW_TABLE_BYTES;
    bufferInfo.props = SDL_CreateProperties();
    SDL_SetStringProperty(bufferInfo.props, SDL_PROP_GPU_BUFFER_CREATE_NAME_STRING, "glow table");
    buffer.m_buffer = Gpu<SDL_GPUBuffer>{ SDL_CreateGPUBuffer(device, &bufferInfo), { device } };
    SDL_DestroyProperties(bufferInfo.props);
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = GLOW_TABLE_BYTES;
    buffer.m_transfer = Gpu<SDL_GPUTransferBuffer>{ SDL_CreateGPUTransferBuffer(device, &transferInfo), { device } };
    SDL_GPUCommandBuffer* commands =
        buffer.m_buffer == nullptr || buffer.m_transfer == nullptr ? nullptr : SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        return std::unexpected{ core::Error{ std::format("the glow table wasn't created: {}", SDL_GetError()) } };
    }
    // Filled before any frame binds it, so no draw reads it empty.
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    const std::expected<void, core::Error> uploaded = buffer.Upload(copy, table);
    SDL_EndGPUCopyPass(copy);
    if (!uploaded) {
        SDL_CancelGPUCommandBuffer(commands);
        return std::unexpected{ uploaded.error() };
    }
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return std::unexpected{ core::Error{ std::format("the glow table wasn't uploaded: {}", SDL_GetError()) } };
    }
    return buffer;
}

std::expected<void, core::Error> GlowTableBuffer::Upload(
    SDL_GPUCopyPass* copy,
    const std::array<GlowRecord, core::GLOW_ROLES>& table
) {
    PN_PROFILE_ZONE();
    const std::span<const std::byte> bytes = std::as_bytes(std::span{ table });
    void* mapped = SDL_MapGPUTransferBuffer(m_device, m_transfer.get(), true);
    if (mapped == nullptr) {
        return std::unexpected{ core::Error{ std::format("the glow table wasn't uploaded: {}", SDL_GetError()) } };
    }
    std::ranges::copy(bytes, BytesAt(mapped, bytes.size()).begin());
    SDL_UnmapGPUTransferBuffer(m_device, m_transfer.get());
    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = m_transfer.get();
    SDL_GPUBufferRegion destination{};
    destination.buffer = m_buffer.get();
    destination.size = static_cast<u32>(bytes.size());
    SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    return {};
}

std::expected<FogTexture, core::Error> FogTexture::Create(SDL_GPUDevice* device) {
    FogTexture fog;
    fog.m_device = device;
    fog.m_clear = CreateFogTexture(device, 1, 1, "no fog");
    const Gpu<SDL_GPUTransferBuffer> transfer = CreateTransfer(device, FOG_TEXEL_BYTES);
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = SDL_GPU_FILTER_LINEAR;
    info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_SAMPLER_CREATE_NAME_STRING, "fog");
    fog.m_sampler = Gpu<SDL_GPUSampler>{ SDL_CreateGPUSampler(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    SDL_GPUCommandBuffer* commands = fog.m_clear == nullptr || transfer == nullptr || fog.m_sampler == nullptr
                                         ? nullptr
                                         : SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        return std::unexpected{ core::Error{ std::format("the fog texture wasn't created: {}", SDL_GetError()) } };
    }
    // Filled before any frame binds it, so no draw reads it undefined.
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    constexpr std::array<u8, FOG_TEXEL_BYTES> CLEAR{ { FOG_FULL, FOG_FULL } };
    const bool copied = CopyFog(device, copy, transfer.get(), fog.m_clear.get(), CLEAR, 1, 1);
    SDL_EndGPUCopyPass(copy);
    if (!copied) {
        SDL_CancelGPUCommandBuffer(commands);
        return std::unexpected{ core::Error{ std::format("the fog texture wasn't uploaded: {}", SDL_GetError()) } };
    }
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return std::unexpected{ core::Error{ std::format("the fog texture wasn't uploaded: {}", SDL_GetError()) } };
    }
    return fog;
}

std::expected<void, core::Error> FogTexture::Update(SDL_GPUCopyPass* copy, const FogFrame& fog, const core::FogLook& look) {
    PN_PROFILE_ZONE();
    m_uniforms = FogUniformsOf(fog, look);
    if (m_uniforms.on == 0) {
        return {};
    }
    const std::size_t bytes = std::size_t{ fog.columns } * fog.rows * FOG_TEXEL_BYTES;
    if (fog.values.size() < bytes) {
        m_uniforms.on = 0;
        return std::unexpected{ core::Error{ std::format(
            "the fog has {} values for {} by {} tiles, so it isn't drawn", fog.values.size(), fog.columns, fog.rows
        ) } };
    }
    if (m_field == nullptr || fog.columns != m_columns || fog.rows != m_rows) {
        m_held.reset();
        m_columns = fog.columns;
        m_rows = fog.rows;
        m_field = CreateFogTexture(m_device, m_columns, m_rows, "fog");
        m_transfer = CreateTransfer(m_device, static_cast<u32>(bytes));
        if (m_field == nullptr || m_transfer == nullptr) {
            m_field.reset();
            m_uniforms.on = 0;
            return std::unexpected{ core::Error{
                std::format("the fog's {} by {} texture wasn't created: {}", m_columns, m_rows, SDL_GetError()) } };
        }
    }
    if (m_held == fog.revision) {
        return {};
    }
    if (!CopyFog(m_device, copy, m_transfer.get(), m_field.get(), fog.values.first(bytes), m_columns, m_rows)) {
        // The last submitted values are only a little stale, and dropping the fog would show the whole map for a frame;
        // with none held yet the texture is undefined, so the frame draws without the fog.
        if (!m_held) {
            m_uniforms.on = 0;
        }
        return std::unexpected{ core::Error{ std::format("the fog wasn't uploaded: {}", SDL_GetError()) } };
    }
    m_recorded = fog.revision;
    return {};
}

void FogTexture::Submitted(bool submitted) {
    if (submitted && m_recorded) {
        m_held = m_recorded;
    }
    m_recorded.reset();
}

SDL_GPUTextureSamplerBinding FogTexture::Binding() const {
    return { .texture = m_uniforms.on != 0 ? m_field.get() : m_clear.get(), .sampler = m_sampler.get() };
}

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
    // Cycled: a frame in flight may still read what the transfer buffer and the storage buffer held.
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
    f32 alpha,
    const GlowFrame& glows,
    const FogTexture& fog
) const {
    PN_PROFILE_ZONE();
    if (m_count == 0) {
        return;
    }
    Bind(commands, pass, m_instances.get(), sprites, glows, fog);
    DrawUniforms uniforms = UniformsOf(transform, alpha, glows);
    for (const core::Batch& batch : batches) {
        if (batch.first >= m_count) {
            continue; // batches may come in any order of their instances (the overlay's do)
        }
        // Bindings outlive a pipeline change within the pass, so only the pipeline is rebound.
        SDL_BindGPUGraphicsPipeline(pass, m_pipelines.at(static_cast<std::size_t>(batch.pipeline)).get());
        uniforms.firstInstance = batch.first;
        SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));
        // first_vertex and first_instance stay 0: SDL warns they don't reach SV_VertexID and SV_InstanceID.
        SDL_DrawGPUPrimitives(pass, VERTICES_PER_QUAD, std::min(batch.count, m_count - batch.first), 0, 0);
    }
}

void SpriteRenderer::DrawGlowOnly(
    SDL_GPUCommandBuffer* commands,
    SDL_GPURenderPass* pass,
    u32 instance,
    core::GlowRole role,
    const SpriteArray& sprites,
    const ClipTransform& transform,
    f32 alpha,
    const GlowFrame& glows,
    const FogTexture& fog
) const {
    PN_PROFILE_ZONE();
    if (instance >= m_count || role == core::GlowRole::None) {
        return;
    }
    Bind(commands, pass, m_instances.get(), sprites, glows, fog);
    SDL_BindGPUGraphicsPipeline(pass, m_pipelines.at(static_cast<std::size_t>(core::Pipeline::Normal)).get());
    DrawUniforms uniforms = UniformsOf(transform, alpha, glows);
    uniforms.firstInstance = instance;
    uniforms.glowOnlyRole = std::to_underlying(role);
    SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));
    SDL_DrawGPUPrimitives(pass, VERTICES_PER_QUAD, 1, 0, 0);
}

} // namespace gfx::internal
