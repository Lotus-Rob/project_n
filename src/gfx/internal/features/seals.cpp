#include "gfx/internal/features/seals.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "gfx/internal/features/meshes.hpp"
#include "gfx/internal/resources/shaders.hpp"

namespace gfx::internal {

namespace {

constexpr u32 SEAL_VERTEX_BUFFER_BYTES = MAX_SEAL_QUADS * QUAD_VERTICES * static_cast<u32>(SEAL_VERTEX_BYTES);
constexpr u32 SEAL_INDEX_BYTES = MAX_SEAL_QUADS * QUAD_INDICES * static_cast<u32>(sizeof(u32));
constexpr u32 SEAL_LAYER_BUFFER_BYTES = MAX_SEAL_LAYERS_DRAWN * static_cast<u32>(SEAL_GPU_LAYER_BYTES);
// A storage buffer may not be empty: the smallest pack's room, grown as packs need.
constexpr u32 SEAL_SMALLEST_PACK = 64;
static_assert(
    MAX_SEALS_DRAWN * (core::MAX_SEAL_LAYERS + 1) <= MAX_SEAL_LAYERS_DRAWN,
    "the layer buffer holds every layer DrawSeals() draws, so no quad points past it"
);

// `Draw` in shaders/seal.vert.hlsl: rows of 16 bytes, as HLSL packs a constant buffer.
constexpr std::size_t SEAL_UNIFORM_BYTES = 32;
constexpr f32 SEAL_CLIP_SPAN = 2.0F; // clip space spans this much across either axis
struct SealUniforms {
    std::array<f32, 4> worldToClip{};
    std::array<f32, 2> pixelsPerClip{};
    u32 wash = 0;
    u32 padding = 0;
};
static_assert(sizeof(SealUniforms) == SEAL_UNIFORM_BYTES, "the seal vertex shader's constant buffer is 32 bytes");

// The constants the seal shaders copy, as they hold them: each must equal what it copies.
constexpr u32 SHADER_SEAL_STAR = 4;     // seal.hlsli's SEAL_STAR
constexpr u32 SHADER_SEAL_FLOURISH = 6; // seal.hlsli's SEAL_FLOURISH
constexpr u32 SHADER_SEAL_EMBLEM = 7;   // seal.hlsli's SEAL_EMBLEM
static_assert(
    std::to_underlying(core::SealPartKind::Circle) == 0 && std::to_underlying(core::SealPartKind::Ticks) == 1 &&
        std::to_underlying(core::SealPartKind::Dots) == 2 && std::to_underlying(core::SealPartKind::Script) == 3 &&
        std::to_underlying(core::SealPartKind::Star) == SHADER_SEAL_STAR &&
        std::to_underlying(core::SealPartKind::Polygon) == SHADER_SEAL_STAR + 1 &&
        std::to_underlying(core::SealPartKind::Flourish) == SHADER_SEAL_FLOURISH,
    "seal.hlsli's SEAL_ constants are core::SealPartKind's values"
);
static_assert(SEAL_EMBLEM_KIND == SHADER_SEAL_EMBLEM, "seal.hlsli's SEAL_EMBLEM is gfx::SEAL_EMBLEM_KIND");

// `Look` in shaders/seal.frag.hlsl, the fragment uniform: the table's halo and the band margin the bands were laid out with
// (gfx::SealBandMargin() of the same reach), so the shader's frame mask ends where the geometry does.
constexpr std::size_t SEAL_LOOK_UNIFORM_BYTES = 16;
struct SealLookUniforms {
    f32 haloReach = 0.0F;
    f32 haloStrength = 0.0F;
    f32 bandMargin = 0.0F;
    f32 padding = 0.0F;
};
static_assert(sizeof(SealLookUniforms) == SEAL_LOOK_UNIFORM_BYTES, "the seal fragment shader's constant buffer is 16 bytes");

Gpu<SDL_GPUGraphicsPipeline> CreateSealPipeline(SDL_GPUDevice* device, SDL_GPUTextureFormat format, bool wash) {
    const Gpu<SDL_GPUShader> vertex = CreateShader(device, shaders::SEAL_VERT, SDL_GPU_SHADERSTAGE_VERTEX);
    const Gpu<SDL_GPUShader> fragment = CreateShader(device, shaders::SEAL_FRAG, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (vertex == nullptr || fragment == nullptr) {
        return { nullptr, { device } };
    }
    SDL_GPUColorTargetDescription target{};
    target.format = format;
    // Premultiplied alpha's blends (§8.8): the wash's ink covers what is under it, the lines' light adds to it.
    const SDL_GPUBlendFactor destination = wash ? SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA : SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_color_blendfactor = destination;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = destination;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    const SDL_GPUVertexBufferDescription buffer{
        .slot = 0, .pitch = SEAL_VERTEX_BYTES, .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX, .instance_step_rate = 0
    };
    const std::array<SDL_GPUVertexAttribute, 4> attributes{ {
        { .location = 0,
          .buffer_slot = 0,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
          .offset = offsetof(SealVertex, position) },
        { .location = 1,
          .buffer_slot = 0,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
          .offset = offsetof(SealVertex, local) },
        { .location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UINT, .offset = offsetof(SealVertex, layer) },
        { .location = 3, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, .offset = offsetof(SealVertex, grow) },
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
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, wash ? "seal washes" : "seal lines");
    // The pipeline keeps what it needs of the shaders, which are released on return.
    Gpu<SDL_GPUGraphicsPipeline> pipeline{ SDL_CreateGPUGraphicsPipeline(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return pipeline;
}

Gpu<SDL_GPUTransferBuffer> CreateUploadBuffer(SDL_GPUDevice* device, u32 bytes) {
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = bytes;
    return Gpu<SDL_GPUTransferBuffer>{ SDL_CreateGPUTransferBuffer(device, &info), { device } };
}

} // namespace

std::expected<SealRenderer, core::Error> SealRenderer::Create(SDL_GPUDevice* device, SDL_GPUTextureFormat format) {
    SealRenderer renderer;
    renderer.m_device = device;
    renderer.m_wash = CreateSealPipeline(device, format, true);
    renderer.m_lines = CreateSealPipeline(device, format, false);
    if (renderer.m_wash == nullptr || renderer.m_lines == nullptr) {
        return std::unexpected{ core::Error{ std::format("a seal pipeline was rejected: {}", SDL_GetError()) } };
    }
    renderer.m_vertices = CreateMeshBuffer(device, SDL_GPU_BUFFERUSAGE_VERTEX, SEAL_VERTEX_BUFFER_BYTES, "seal vertices");
    renderer.m_indices = CreateMeshBuffer(device, SDL_GPU_BUFFERUSAGE_INDEX, SEAL_INDEX_BYTES, "seal quad indices");
    renderer.m_layers =
        CreateMeshBuffer(device, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, SEAL_LAYER_BUFFER_BYTES, "seal layers");
    renderer.m_vertexTransfer = CreateUploadBuffer(device, SEAL_VERTEX_BUFFER_BYTES);
    renderer.m_layerTransfer = CreateUploadBuffer(device, SEAL_LAYER_BUFFER_BYTES);
    const bool packRoom = renderer.Reserve(
                              renderer.m_parts,
                              renderer.m_partTransfer,
                              renderer.m_partBytes,
                              std::size_t{ SEAL_SMALLEST_PACK } * SEAL_GPU_PART_BYTES,
                              "seal parts"
                          ) &&
                          renderer.Reserve(
                              renderer.m_sprites,
                              renderer.m_spriteTransfer,
                              renderer.m_spriteBytes,
                              std::size_t{ SEAL_SMALLEST_PACK } * SEAL_GPU_SPRITE_BYTES,
                              "seal sprites"
                          );
    if (renderer.m_vertices == nullptr || renderer.m_indices == nullptr || renderer.m_layers == nullptr ||
        renderer.m_vertexTransfer == nullptr || renderer.m_layerTransfer == nullptr || !packRoom) {
        return std::unexpected{ core::Error{ std::format("the seal buffers weren't created: {}", SDL_GetError()) } };
    }
    return renderer;
}

void SealRenderer::SetPack(SealPack pack, const core::Seals& seals) {
    m_haloReach = seals.haloReach;
    m_haloStrength = seals.haloStrength;
    m_pack = std::move(pack);
    ++m_packVersion;
}

bool SealRenderer::Reserve(
    Gpu<SDL_GPUBuffer>& buffer,
    Gpu<SDL_GPUTransferBuffer>& transfer,
    u32& capacity,
    std::size_t bytes,
    const char* name
) {
    const std::optional<u32> room = SealBufferRoom(capacity, bytes);
    if (!room) {
        return false;
    }
    if (*room == capacity) {
        return true;
    }
    // A frame in flight keeps the old ones, which SDL releases once it is done with them.
    buffer = CreateMeshBuffer(m_device, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, *room, name);
    transfer = CreateUploadBuffer(m_device, *room);
    capacity = buffer != nullptr && transfer != nullptr ? *room : 0;
    return capacity != 0;
}

std::expected<void, core::Error> SealRenderer::UploadPack(SDL_GPUCopyPass* copy) {
    PN_PROFILE_ZONE();
    const std::span<const std::byte> parts = std::as_bytes(std::span{ m_pack.parts });
    const std::span<const std::byte> sprites = std::as_bytes(std::span{ m_pack.sprites });
    if (!Reserve(m_parts, m_partTransfer, m_partBytes, parts.size(), "seal parts") ||
        !Reserve(m_sprites, m_spriteTransfer, m_spriteBytes, sprites.size(), "seal sprites")) {
        return std::unexpected{ core::Error{ std::format(
            "the seals' pack ({} bytes of parts, {} of sprites) had no room: {}", parts.size(), sprites.size(), SDL_GetError()
        ) } };
    }
    // Cycled: a frame in flight may still read the pack before.
    if ((!parts.empty() && !CopyToBuffer(m_device, copy, m_partTransfer.get(), m_parts.get(), parts, true)) ||
        (!sprites.empty() && !CopyToBuffer(m_device, copy, m_spriteTransfer.get(), m_sprites.get(), sprites, true))) {
        return std::unexpected{ core::Error{ std::format("the seals' pack wasn't uploaded: {}", SDL_GetError()) } };
    }
    m_packRecorded = m_packVersion;
    return {};
}

std::expected<std::size_t, core::Error> SealRenderer::Upload(SDL_GPUCopyPass* copy, const SealDrawing& drawing) {
    PN_PROFILE_ZONE();
    m_groundWash = m_groundLines = m_aboveWash = m_aboveLines = {};
    if (!m_indicesUploaded) {
        if (!UploadQuadIndices(m_device, copy, m_indices.get(), MAX_SEAL_QUADS)) {
            return std::unexpected{ core::Error{ std::format("the seal quad indices weren't uploaded: {}", SDL_GetError()) } };
        }
        m_indicesUploaded = true;
    }
    if (m_packHeld != m_packVersion) {
        if (std::expected<void, core::Error> packed = UploadPack(copy); !packed) {
            return std::unexpected{ std::move(packed.error()) };
        }
    }
    // The lists end to end, each as much of it as still fits.
    m_quads.clear();
    std::size_t dropped = 0;
    const auto add = [&](const std::vector<SealVertex>& vertices, Run& run) {
        const std::size_t quads = vertices.size() / QUAD_VERTICES;
        const std::size_t room = MAX_SEAL_QUADS - (m_quads.size() / QUAD_VERTICES);
        const std::size_t fitting = std::min(quads, room);
        run = { .first = static_cast<u32>(m_quads.size() / QUAD_VERTICES), .quads = static_cast<u32>(fitting) };
        m_quads.insert(
            m_quads.end(), vertices.begin(), vertices.begin() + static_cast<std::ptrdiff_t>(fitting * QUAD_VERTICES)
        );
        dropped += quads - fitting;
    };
    add(drawing.groundWash, m_groundWash);
    add(drawing.groundLines, m_groundLines);
    add(drawing.aboveWash, m_aboveWash);
    add(drawing.aboveLines, m_aboveLines);
    if (m_quads.empty()) {
        return dropped;
    }
    PN_ASSERT(drawing.layers.size() <= MAX_SEAL_LAYERS_DRAWN, "a frame drew more seal layers than DrawSeals() can");
    const std::span<const SealGpuLayer> layers =
        std::span{ drawing.layers }.first(std::min<std::size_t>(drawing.layers.size(), MAX_SEAL_LAYERS_DRAWN));
    // Cycled: a frame in flight may still read what the buffers held (§8.7).
    if (!CopyToBuffer(m_device, copy, m_vertexTransfer.get(), m_vertices.get(), std::as_bytes(std::span{ m_quads }), true) ||
        !CopyToBuffer(m_device, copy, m_layerTransfer.get(), m_layers.get(), std::as_bytes(layers), true)) {
        m_groundWash = m_groundLines = m_aboveWash = m_aboveLines = {};
        return std::unexpected{ core::Error{ std::format("the seals weren't uploaded: {}", SDL_GetError()) } };
    }
    return dropped;
}

void SealRenderer::Submitted(bool submitted) {
    if (submitted && m_packRecorded) {
        m_packHeld = *m_packRecorded;
    }
    m_packRecorded.reset();
}

void SealRenderer::DrawGround(
    SDL_GPUCommandBuffer* commands,
    SDL_GPURenderPass* pass,
    const SealTarget& target,
    const SpriteArray& sprites
) const {
    PN_PROFILE_ZONE();
    DrawRuns(commands, pass, target, sprites, m_groundWash, m_groundLines);
}

void SealRenderer::DrawAbove(
    SDL_GPUCommandBuffer* commands,
    SDL_GPURenderPass* pass,
    const SealTarget& target,
    const SpriteArray& sprites
) const {
    PN_PROFILE_ZONE();
    DrawRuns(commands, pass, target, sprites, m_aboveWash, m_aboveLines);
}

void SealRenderer::DrawRuns(
    SDL_GPUCommandBuffer* commands,
    SDL_GPURenderPass* pass,
    const SealTarget& target,
    const SpriteArray& sprites,
    Run wash,
    Run lines
) const {
    if (wash.quads == 0 && lines.quads == 0) {
        return;
    }
    const SDL_GPUBufferBinding vertices{ .buffer = m_vertices.get(), .offset = 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
    const SDL_GPUBufferBinding indices{ .buffer = m_indices.get(), .offset = 0 };
    SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    const SDL_GPUTextureSamplerBinding field{ .texture = sprites.field.get(), .sampler = sprites.fieldSampler.get() };
    SDL_BindGPUFragmentSamplers(pass, 0, &field, 1);
    const std::array<SDL_GPUBuffer*, 3> storage{ m_layers.get(), m_parts.get(), m_sprites.get() };
    SDL_BindGPUFragmentStorageBuffers(pass, 0, storage.data(), static_cast<u32>(storage.size()));
    SDL_BindGPUVertexStorageBuffers(pass, 0, storage.data(), 1); // the layers, for their axes
    const ClipTransform& worldToClip = target.worldToClip;
    SealUniforms uniforms{ .worldToClip = { worldToClip.scaleX, worldToClip.scaleY, worldToClip.offsetX, worldToClip.offsetY },
                           .pixelsPerClip = { static_cast<f32>(target.width) / SEAL_CLIP_SPAN,
                                              static_cast<f32>(target.height) / SEAL_CLIP_SPAN } };
    const SealLookUniforms look{ .haloReach = m_haloReach,
                                 .haloStrength = m_haloStrength,
                                 .bandMargin = SealBandMargin(m_haloReach) };
    // The washes first, under every seal's lines: ink, then light.
    for (const auto& [run, pipeline, isWash] :
         { std::tuple{ wash, m_wash.get(), true }, std::tuple{ lines, m_lines.get(), false } }) {
        if (run.quads == 0) {
            continue;
        }
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        uniforms.wash = isWash ? 1U : 0U;
        SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));
        SDL_PushGPUFragmentUniformData(commands, 0, &look, sizeof(look));
        // The run's vertices are reached through the vertex offset; no shader reads SV_VertexID (§8.5).
        SDL_DrawGPUIndexedPrimitives(pass, run.quads * QUAD_INDICES, 1, 0, static_cast<i32>(run.first * QUAD_VERTICES), 0);
    }
}

} // namespace gfx::internal
