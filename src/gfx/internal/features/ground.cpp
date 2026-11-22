#include "gfx/internal/features/ground.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "gfx/fog.hpp"
#include "gfx/ground_field.hpp"
#include "gfx/internal/resources/shaders.hpp"

namespace gfx::internal {

namespace {

/// Two bytes a texel: the kind index, then the shore byte, as GroundTexels() writes them.
constexpr u32 FIELD_TEXEL_BYTES = 2;
/// The ground's quad: a triangle strip of four corners, from the vertex index alone.
constexpr u32 QUAD_CORNERS = 4;
/// The fragment stage's uniform slots: the fog's first, as every land shader has it, then the ground's.
constexpr u32 FOG_UNIFORM_SLOT = 0;
constexpr u32 GROUND_UNIFORM_SLOT = 1;
/// The constants the ground shaders copy, as they hold them: each must equal what it copies.
constexpr u32 SHADER_GROUND_KINDS = 16;    // ground.hlsli's washes[] length and ground.frag.hlsl's LAST_KIND + 1
constexpr u32 SHADER_MODE_VEIL = 1;        // ground.hlsli's GROUND_MODE_VEIL
constexpr u32 SHADER_MODE_PAPER = 2;       // ground.hlsli's GROUND_MODE_PAPER
constexpr u32 SHADER_MODE_CLOUD_SHADE = 3; // ground.hlsli's GROUND_MODE_CLOUD_SHADE
static_assert(core::MAX_GROUND_KINDS == SHADER_GROUND_KINDS, "the ground shaders' wash table holds MAX_GROUND_KINDS");
static_assert(
    GROUND_MODE_GROUND == 0 && GROUND_MODE_VEIL == SHADER_MODE_VEIL && GROUND_MODE_PAPER == SHADER_MODE_PAPER &&
        GROUND_MODE_CLOUD_SHADE == SHADER_MODE_CLOUD_SHADE,
    "ground.hlsli's GROUND_MODE_ constants are gfx's"
);

/// `GroundPlace` in shaders/ground.hlsli.
struct GroundPlace {
    std::array<f32, 4> worldToClip{};
    std::array<f32, 4> quad{};
};

Gpu<SDL_GPUGraphicsPipeline> CreateGroundPipeline(SDL_GPUDevice* device, SDL_GPUTextureFormat format) {
    const Gpu<SDL_GPUShader> vertex = CreateShader(device, shaders::GROUND_VERT, SDL_GPU_SHADERSTAGE_VERTEX);
    const Gpu<SDL_GPUShader> fragment = CreateShader(device, shaders::GROUND_FRAG, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (vertex == nullptr || fragment == nullptr) {
        return { nullptr, { device } };
    }
    SDL_GPUColorTargetDescription target{};
    target.format = format;
    // Premultiplied over: the ground and the paper are opaque, the veil lies over what is drawn.
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex.get();
    info.fragment_shader = fragment.get();
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, "ground");
    // The pipeline keeps what it needs of the shaders, which are released on return.
    Gpu<SDL_GPUGraphicsPipeline> pipeline{ SDL_CreateGPUGraphicsPipeline(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return pipeline;
}

/// A sampler clamped at the edge, filtering by `filter`, named `name` for graphics debuggers.
Gpu<SDL_GPUSampler> CreateFieldSampler(SDL_GPUDevice* device, SDL_GPUFilter filter, const char* name) {
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = filter;
    info.mag_filter = filter;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_SAMPLER_CREATE_NAME_STRING, name);
    Gpu<SDL_GPUSampler> sampler{ SDL_CreateGPUSampler(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return sampler;
}

Gpu<SDL_GPUTexture> CreateFieldTexture(SDL_GPUDevice* device, u32 columns, u32 rows) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    // UNORM, not sRGB: the shader reads each byte back as it was written, divided by 255.
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = columns;
    info.height = rows;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, "ground field");
    Gpu<SDL_GPUTexture> texture{ SDL_CreateGPUTexture(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return texture;
}

} // namespace

std::expected<GroundRenderer, core::Error> GroundRenderer::Create(SDL_GPUDevice* device, SDL_GPUTextureFormat format) {
    GroundRenderer renderer;
    renderer.m_device = device;
    renderer.m_pipeline = CreateGroundPipeline(device, format);
    if (renderer.m_pipeline == nullptr) {
        return std::unexpected{ core::Error{ std::format("the ground pipeline was rejected: {}", SDL_GetError()) } };
    }
    renderer.m_point = CreateFieldSampler(device, SDL_GPU_FILTER_NEAREST, "ground kinds");
    renderer.m_linear = CreateFieldSampler(device, SDL_GPU_FILTER_LINEAR, "ground shore");
    if (renderer.m_point == nullptr || renderer.m_linear == nullptr) {
        return std::unexpected{ core::Error{ std::format("the ground samplers weren't created: {}", SDL_GetError()) } };
    }
    return renderer;
}

std::expected<void, core::Error> GroundRenderer::Update(
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
    if (m_field == nullptr || map.Columns() != m_columns || map.Rows() != m_rows) {
        m_held.reset();
        m_columns = map.Columns();
        m_rows = map.Rows();
        m_field = CreateFieldTexture(m_device, m_columns, m_rows);
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = m_columns * m_rows * FIELD_TEXEL_BYTES;
        m_transfer = Gpu<SDL_GPUTransferBuffer>{ SDL_CreateGPUTransferBuffer(m_device, &transferInfo), { m_device } };
        if (m_field == nullptr || m_transfer == nullptr) {
            m_field.reset();
            return std::unexpected{ core::Error{
                std::format("the ground's {} by {} field wasn't created: {}", m_columns, m_rows, SDL_GetError()) } };
        }
    }
    const std::array<u8, 2> water{ { look.ground.shallowKind, look.ground.deepKind } };
    GroundTexels(map, water, m_texels);
    void* mapped = SDL_MapGPUTransferBuffer(m_device, m_transfer.get(), true);
    if (mapped == nullptr) {
        return std::unexpected{ core::Error{ std::format("the ground's field wasn't uploaded: {}", SDL_GetError()) } };
    }
    std::ranges::copy(std::as_bytes(std::span{ m_texels }), BytesAt(mapped, m_texels.size()).begin());
    SDL_UnmapGPUTransferBuffer(m_device, m_transfer.get());
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = m_transfer.get();
    source.pixels_per_row = m_columns;
    source.rows_per_layer = m_rows;
    SDL_GPUTextureRegion region{};
    region.texture = m_field.get();
    region.w = m_columns;
    region.h = m_rows;
    region.d = 1;
    // Cycled: a frame in flight may still read the field it had.
    SDL_UploadToGPUTexture(copy, &source, &region, true);
    m_recorded = version;
    m_ready = true;
    return {};
}

void GroundRenderer::Submitted(bool submitted) {
    if (submitted && m_recorded) {
        m_held = m_recorded;
    }
    m_recorded.reset();
}

void GroundRenderer::Draw(
    SDL_GPUCommandBuffer* commands,
    SDL_GPURenderPass* pass,
    const ClipTransform& worldToClip,
    const core::Rect& quad,
    const GroundUniforms& uniforms,
    const FogTexture& fog
) const {
    PN_PROFILE_ZONE();
    if (!m_ready) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(pass, m_pipeline.get());
    const GroundPlace place{
        .worldToClip = { { worldToClip.scaleX, worldToClip.scaleY, worldToClip.offsetX, worldToClip.offsetY } },
        .quad = { { quad.min.x, quad.min.y, quad.max.x, quad.max.y } }
    };
    SDL_PushGPUVertexUniformData(commands, 0, &place, sizeof(place));
    const std::array<SDL_GPUTextureSamplerBinding, 3> textures{ {
        { .texture = m_field.get(), .sampler = m_point.get() },
        { .texture = m_field.get(), .sampler = m_linear.get() },
        fog.Binding(),
    } };
    SDL_BindGPUFragmentSamplers(pass, 0, textures.data(), static_cast<u32>(textures.size()));
    SDL_PushGPUFragmentUniformData(commands, FOG_UNIFORM_SLOT, &fog.Uniforms(), sizeof(FogUniforms));
    SDL_PushGPUFragmentUniformData(commands, GROUND_UNIFORM_SLOT, &uniforms, sizeof(GroundUniforms));
    SDL_DrawGPUPrimitives(pass, QUAD_CORNERS, 1, 0, 0);
}

} // namespace gfx::internal
