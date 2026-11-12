#include "gfx/internal/resources/sprite_array.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/distance_field.hpp"
#include "gfx/internal/resources/png.hpp"
#include "gfx/sprite_block.hpp"
#include "platform/clock.hpp"

namespace gfx::internal {

namespace {

constexpr SDL_GPUTextureFormat FIELD_FORMAT = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
// D3D12 wants a texture upload's offset into its buffer to be a multiple of this.
constexpr std::size_t UPLOAD_ALIGNMENT = 512;
constexpr SDL_GPUTextureUsageFlags ARRAY_USAGE = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;

Gpu<SDL_GPUTexture> CreateArray(
    SDL_GPUDevice* device,
    const core::Manifest& manifest,
    SDL_GPUTextureFormat format,
    const char* name
) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    info.format = format;
    // A colour target too: SDL_GenerateMipmapsForGPUTexture blits into it, and its layers are cleared by render passes.
    info.usage = ARRAY_USAGE;
    info.width = manifest.pageSize;
    info.height = manifest.pageSize;
    info.layer_count_or_depth = manifest.pages;
    info.num_levels = manifest.mipLevels;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, name);
    Gpu<SDL_GPUTexture> texture{ SDL_CreateGPUTexture(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return texture;
}

Gpu<SDL_GPUSampler> CreateSampler(SDL_GPUDevice* device, u32 mipLevels, const char* name) {
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = SDL_GPU_FILTER_LINEAR;
    info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.max_lod = static_cast<f32>(mipLevels - 1); // 0 by default, which would read the top mip only
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_SAMPLER_CREATE_NAME_STRING, name);
    Gpu<SDL_GPUSampler> sampler{ SDL_CreateGPUSampler(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return sampler;
}

// What the sprites' blocks don't cover must be transparent: a fresh texture's contents are undefined.
void ClearPages(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* texture, u32 pages, f32 value) {
    for (u32 page = 0; page < pages; ++page) {
        SDL_GPUColorTargetInfo target{};
        target.texture = texture;
        target.layer_or_depth_plane = page;
        target.clear_color = { .r = value, .g = value, .b = value, .a = value };
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_EndGPURenderPass(SDL_BeginGPURenderPass(commands, &target, 1, nullptr));
    }
}

// Records the copy of the texels at `offset` of `transfer` into `block` of `texture`'s layer.
void QueueBlock(
    SDL_GPUCopyPass* copy,
    SDL_GPUTransferBuffer* transfer,
    u32 offset,
    SDL_GPUTexture* texture,
    u32 layer,
    const SpriteBlock& block
) {
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.offset = offset;
    source.pixels_per_row = block.width;
    source.rows_per_layer = block.height;
    SDL_GPUTextureRegion region{};
    region.texture = texture;
    region.layer = layer;
    region.x = block.x;
    region.y = block.y;
    region.w = block.width;
    region.h = block.height;
    region.d = 1;
    SDL_UploadToGPUTexture(copy, &source, &region, false); // never cycled: the whole array would be (§8.7)
}

} // namespace

u64 FieldBytes(const core::Manifest& manifest) {
    u64 bytes = 0;
    for (u32 level = 0; level < manifest.mipLevels; ++level) {
        const u64 side = std::max<u64>(manifest.pageSize >> level, 1);
        bytes += side * side * manifest.pages;
    }
    return bytes;
}

std::expected<SpriteArray, core::Error> CreateSpriteArray(SDL_GPUDevice* device, const core::Manifest& manifest) {
    // The mips are blitted: both usages, or no field array.
    if (!SDL_GPUTextureSupportsFormat(device, FIELD_FORMAT, SDL_GPU_TEXTURETYPE_2D_ARRAY, ARRAY_USAGE)) {
        return std::unexpected{ core::Error{
            "this GPU can't use R8_UNORM as a sampled texture and a colour target, which the sprites' distance fields need" } };
    }
    SpriteArray array{ .texture = CreateArray(device, manifest, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, "sprite atlas"),
                       .sampler = CreateSampler(device, manifest.mipLevels, "sprite atlas"),
                       .field = CreateArray(device, manifest, FIELD_FORMAT, "sprite fields"),
                       .fieldSampler = CreateSampler(device, manifest.mipLevels, "sprite fields") };
    if (array.texture == nullptr || array.sampler == nullptr) {
        return std::unexpected{ core::Error{ std::format("the sprite atlas texture wasn't created: {}", SDL_GetError()) } };
    }
    if (array.field == nullptr || array.fieldSampler == nullptr) {
        return std::unexpected{ core::Error{
            std::format("the sprites' distance field texture wasn't created: {}", SDL_GetError()) } };
    }
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        return std::unexpected{ core::Error{ std::format("no command buffer to clear the sprite atlas: {}", SDL_GetError()) } };
    }
    ClearPages(commands, array.texture.get(), manifest.pages, 0.0F);
    ClearPages(commands, array.field.get(), manifest.pages, 1.0F);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return std::unexpected{ core::Error{ std::format("the sprite atlas wasn't cleared: {}", SDL_GetError()) } };
    }
    return array;
}

std::expected<DecodedSprite, core::Error> DecodeSprite(const std::filesystem::path& assets, const core::SpriteInfo& sprite) {
    // Byte order R, G, B, A on every platform: SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB.
    std::expected<Surface, core::Error> decoded = DecodePng(assets / sprite.file);
    if (!decoded) {
        return std::unexpected{ std::move(decoded.error()) };
    }
    Surface rgba = std::move(*decoded);
    if (std::cmp_not_equal(rgba->w, sprite.width) || std::cmp_not_equal(rgba->h, sprite.height)) {
        return std::unexpected{ core::Error{ std::format(
            "{} is {}x{}, but the atlas expects {}x{}: run `./dev atlas`",
            sprite.file,
            rgba->w,
            rgba->h,
            sprite.width,
            sprite.height
        ) } };
    }
    if (!SDL_PremultiplySurfaceAlpha(rgba.get(), true)) { // in linear space, where the blending happens (§8.4)
        return std::unexpected{ core::Error{ std::format("{} wasn't premultiplied: {}", sprite.file, SDL_GetError()) } };
    }
    const u64 startNs = platform::NowNs();
    const auto pitch = static_cast<std::size_t>(rgba->pitch);
    std::vector<u8> field =
        DistanceField(BytesAt(rgba->pixels, pitch * static_cast<std::size_t>(rgba->h)), pitch, sprite.width, sprite.height);
    return DecodedSprite{ .image = std::move(rgba), .field = std::move(field), .fieldNs = platform::NowNs() - startNs };
}

std::expected<void, core::Error> UploadSprite(
    SDL_GPUDevice* device,
    const SpriteArray& array,
    const core::SpriteInfo& sprite,
    u32 edge,
    const DecodedSprite& decoded
) {
    const SpriteBlock colourBlock = BlockOf(sprite, edge);
    // The field covers the region and its reach, not the gutter's whole edge: a wider gutter keeps the clear.
    const SpriteBlock fieldBlock = FieldBlockOf(sprite);
    const std::size_t colourBytes = static_cast<std::size_t>(colourBlock.width) * colourBlock.height * BYTES_PER_TEXEL;
    const std::size_t fieldOffset = (colourBytes + UPLOAD_ALIGNMENT - 1) / UPLOAD_ALIGNMENT * UPLOAD_ALIGNMENT;
    const std::size_t fieldBytes = static_cast<std::size_t>(fieldBlock.width) * fieldBlock.height;
    if (decoded.field.size() != fieldBytes) {
        return std::unexpected{ core::Error{ std::format(
            "the field of {} has {} bytes, but its block takes {}", sprite.file, decoded.field.size(), fieldBytes
        ) } };
    }
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<u32>(fieldOffset + fieldBytes);
    const Gpu<SDL_GPUTransferBuffer> transfer{ SDL_CreateGPUTransferBuffer(device, &transferInfo), { device } };
    void* mapped = transfer == nullptr ? nullptr : SDL_MapGPUTransferBuffer(device, transfer.get(), false);
    if (mapped == nullptr) {
        return std::unexpected{ core::Error{ std::format("no transfer buffer for {}: {}", sprite.file, SDL_GetError()) } };
    }
    const std::span<std::byte> written = BytesAt(mapped, fieldOffset + fieldBytes);
    const SDL_Surface& image = *decoded.image;
    const auto pitch = static_cast<std::size_t>(image.pitch);
    WriteBlock(
        sprite, edge, BytesAt(image.pixels, pitch * static_cast<std::size_t>(image.h)), pitch, written.first(colourBytes)
    );
    std::ranges::copy(std::as_bytes(std::span{ decoded.field }), written.subspan(fieldOffset).begin());
    SDL_UnmapGPUTransferBuffer(device, transfer.get());

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        return std::unexpected{ core::Error{ std::format("no command buffer for {}: {}", sprite.file, SDL_GetError()) } };
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    QueueBlock(copy, transfer.get(), 0, array.texture.get(), sprite.page, colourBlock);
    QueueBlock(copy, transfer.get(), static_cast<u32>(fieldOffset), array.field.get(), sprite.page, fieldBlock);
    SDL_EndGPUCopyPass(copy);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return std::unexpected{ core::Error{ std::format("{} wasn't uploaded: {}", sprite.file, SDL_GetError()) } };
    }
    return {};
}

std::expected<void, core::Error> GenerateMips(SDL_GPUDevice* device, const SpriteArray& array) {
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        return std::unexpected{ core::Error{ std::format("no command buffer for the sprite mips: {}", SDL_GetError()) } };
    }
    SDL_GenerateMipmapsForGPUTexture(commands, array.texture.get());
    SDL_GenerateMipmapsForGPUTexture(commands, array.field.get());
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return std::unexpected{ core::Error{ std::format("the sprite mips weren't generated: {}", SDL_GetError()) } };
    }
    return {};
}

} // namespace gfx::internal
