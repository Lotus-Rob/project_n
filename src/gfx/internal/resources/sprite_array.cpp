#include "gfx/internal/resources/sprite_array.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/internal/resources/png.hpp"
#include "gfx/sprite_block.hpp"

namespace gfx::internal {

namespace {

Gpu<SDL_GPUTexture> CreateArray(SDL_GPUDevice* device, const core::Manifest& manifest) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    // A colour target too: SDL_GenerateMipmapsForGPUTexture blits into it, and its layers are cleared by render passes.
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = manifest.pageSize;
    info.height = manifest.pageSize;
    info.layer_count_or_depth = manifest.pages;
    info.num_levels = manifest.mipLevels;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, "sprite atlas");
    Gpu<SDL_GPUTexture> texture{ SDL_CreateGPUTexture(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return texture;
}

Gpu<SDL_GPUSampler> CreateSampler(SDL_GPUDevice* device, u32 mipLevels) {
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = SDL_GPU_FILTER_LINEAR;
    info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.max_lod = static_cast<f32>(mipLevels - 1); // 0 by default, which would read the top mip only
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_SAMPLER_CREATE_NAME_STRING, "sprite atlas");
    Gpu<SDL_GPUSampler> sampler{ SDL_CreateGPUSampler(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return sampler;
}

// What the sprites' blocks don't cover must be transparent: a fresh texture's contents are undefined.
void ClearPages(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* texture, u32 pages) {
    for (u32 page = 0; page < pages; ++page) {
        SDL_GPUColorTargetInfo target{};
        target.texture = texture;
        target.layer_or_depth_plane = page;
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_EndGPURenderPass(SDL_BeginGPURenderPass(commands, &target, 1, nullptr));
    }
}

} // namespace

std::expected<SpriteArray, core::Error> CreateSpriteArray(SDL_GPUDevice* device, const core::Manifest& manifest) {
    SpriteArray array{ .texture = CreateArray(device, manifest), .sampler = CreateSampler(device, manifest.mipLevels) };
    if (array.texture == nullptr || array.sampler == nullptr) {
        return std::unexpected{ core::Error{ std::format("the sprite atlas texture wasn't created: {}", SDL_GetError()) } };
    }
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        return std::unexpected{ core::Error{ std::format("no command buffer to clear the sprite atlas: {}", SDL_GetError()) } };
    }
    ClearPages(commands, array.texture.get(), manifest.pages);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return std::unexpected{ core::Error{ std::format("the sprite atlas wasn't cleared: {}", SDL_GetError()) } };
    }
    return array;
}

std::expected<Surface, core::Error> DecodeSprite(const std::filesystem::path& assets, const core::SpriteInfo& sprite) {
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
    return rgba;
}

std::expected<void, core::Error> UploadSprite(
    SDL_GPUDevice* device,
    const SpriteArray& array,
    const core::SpriteInfo& sprite,
    u32 mipLevels,
    const SDL_Surface& image
) {
    const SpriteBlock block = BlockOf(sprite, mipLevels);
    const u32 bytes = block.width * block.height * static_cast<u32>(BYTES_PER_TEXEL);
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = bytes;
    const Gpu<SDL_GPUTransferBuffer> transfer{ SDL_CreateGPUTransferBuffer(device, &transferInfo), { device } };
    void* mapped = transfer == nullptr ? nullptr : SDL_MapGPUTransferBuffer(device, transfer.get(), false);
    if (mapped == nullptr) {
        return std::unexpected{ core::Error{ std::format("no transfer buffer for {}: {}", sprite.file, SDL_GetError()) } };
    }
    const auto pitch = static_cast<std::size_t>(image.pitch);
    WriteBlock(
        sprite, mipLevels, BytesAt(image.pixels, pitch * static_cast<std::size_t>(image.h)), pitch, BytesAt(mapped, bytes)
    );
    SDL_UnmapGPUTransferBuffer(device, transfer.get());

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        return std::unexpected{ core::Error{ std::format("no command buffer for {}: {}", sprite.file, SDL_GetError()) } };
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer.get();
    source.pixels_per_row = block.width;
    source.rows_per_layer = block.height;
    SDL_GPUTextureRegion region{};
    region.texture = array.texture.get();
    region.layer = sprite.page;
    region.x = block.x;
    region.y = block.y;
    region.w = block.width;
    region.h = block.height;
    region.d = 1;
    SDL_UploadToGPUTexture(copy, &source, &region, false); // never cycled: the whole array would be (§8.7)
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
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return std::unexpected{ core::Error{ std::format("the sprite mips weren't generated: {}", SDL_GetError()) } };
    }
    return {};
}

} // namespace gfx::internal
