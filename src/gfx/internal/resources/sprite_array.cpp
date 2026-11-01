#include "gfx/internal/resources/sprite_array.hpp"

#include "core/cxx23.hpp"           // IWYU pragma: keep
#include "core/generated/atlas.hpp" // by `project_n atlas`
#include "core/log.hpp"
#include "platform/clock.hpp"

#include <SDL3_image/SDL_image.h>

namespace gfx::internal {

namespace {

constexpr u32 BYTES_PER_PIXEL = 4; // R8G8B8A8

struct Decoded {
    Surface surface = nullptr;
    u32 offset = 0; // where its pixels start in the transfer buffer
};

std::expected<Surface, core::Error> Decode(const std::filesystem::path& file, const core::atlas::Sprite& sprite) {
    const std::u8string utf8 = file.u8string(); // SDL's paths are UTF-8
    const Surface loaded{ IMG_Load(std::string{ utf8.begin(), utf8.end() }.c_str()) };
    if (loaded == nullptr) {
        return std::unexpected{ core::Error{ std::format("{} wasn't loaded: {}", sprite.file, SDL_GetError()) } };
    }
    // Byte order R, G, B, A on every platform: SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM.
    Surface rgba{ SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32) };
    if (rgba == nullptr) {
        return std::unexpected{ core::Error{ std::format("{} wasn't converted to RGBA: {}", sprite.file, SDL_GetError()) } };
    }
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
    // shortcut: premultiplied in gamma space, the interim default (§8.4); the linear side of the comparison passes true.
    if (!SDL_PremultiplySurfaceAlpha(rgba.get(), false)) {
        return std::unexpected{ core::Error{ std::format("{} wasn't premultiplied: {}", sprite.file, SDL_GetError()) } };
    }
    return rgba;
}

Gpu<SDL_GPUTexture> CreateArray(SDL_GPUDevice* device) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    // A colour target too: SDL_GenerateMipmapsForGPUTexture blits into it, and its layers are cleared by render passes.
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = core::atlas::PAGE_SIZE;
    info.height = core::atlas::PAGE_SIZE;
    info.layer_count_or_depth = core::atlas::PAGES;
    info.num_levels = core::atlas::MIP_LEVELS;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, "sprite atlas");
    Gpu<SDL_GPUTexture> texture{ SDL_CreateGPUTexture(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return texture;
}

Gpu<SDL_GPUSampler> CreateSampler(SDL_GPUDevice* device) {
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = SDL_GPU_FILTER_LINEAR;
    info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.max_lod = static_cast<f32>(core::atlas::MIP_LEVELS - 1); // 0 by default, which would read the top mip only
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_SAMPLER_CREATE_NAME_STRING, "sprite atlas");
    Gpu<SDL_GPUSampler> sampler{ SDL_CreateGPUSampler(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    return sampler;
}

// The gutters around the sprites must be transparent: a fresh texture's contents are undefined.
void ClearPages(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* texture) {
    for (u32 page = 0; page < core::atlas::PAGES; ++page) {
        SDL_GPUColorTargetInfo target{};
        target.texture = texture;
        target.layer_or_depth_plane = page;
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_EndGPURenderPass(SDL_BeginGPURenderPass(commands, &target, 1, nullptr));
    }
}

} // namespace

std::expected<SpriteArray, core::Error> LoadSpriteArray(SDL_GPUDevice* device, const std::filesystem::path& assets) {
    const u64 startNs = platform::NowNs();
    std::vector<Decoded> decoded;
    decoded.reserve(core::atlas::SPRITES.size());
    u32 bytes = 0;
    for (const core::atlas::Sprite& sprite : core::atlas::SPRITES) {
        std::expected<Surface, core::Error> surface = Decode(assets / sprite.file, sprite);
        if (!surface) {
            return std::unexpected{ std::move(surface.error()) };
        }
        decoded.push_back({ .surface = std::move(*surface), .offset = bytes });
        bytes += static_cast<u32>(sprite.width) * sprite.height * BYTES_PER_PIXEL;
    }

    SpriteArray array{ .texture = CreateArray(device), .sampler = CreateSampler(device) };
    if (array.texture == nullptr || array.sampler == nullptr) {
        return std::unexpected{ core::Error{ std::format("the sprite atlas texture wasn't created: {}", SDL_GetError()) } };
    }
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = bytes;
    const Gpu<SDL_GPUTransferBuffer> transfer{ SDL_CreateGPUTransferBuffer(device, &transferInfo), { device } };
    void* mapped = transfer == nullptr ? nullptr : SDL_MapGPUTransferBuffer(device, transfer.get(), false);
    if (mapped == nullptr) {
        return std::unexpected{ core::Error{ std::format("no transfer buffer for the sprite atlas: {}", SDL_GetError()) } };
    }
    const std::span<std::byte> staging = BytesAt(mapped, bytes);
    for (std::size_t i = 0; i < decoded.size(); ++i) {
        const SDL_Surface& surface = *decoded[i].surface;
        const auto pitch = static_cast<std::size_t>(surface.pitch);
        const auto rows = static_cast<std::size_t>(surface.h);
        const std::size_t row = core::atlas::SPRITES.at(i).width * std::size_t{ BYTES_PER_PIXEL };
        const std::span<const std::byte> pixels = BytesAt(surface.pixels, pitch * rows);
        for (std::size_t line = 0; line < rows; ++line) {
            std::ranges::copy(pixels.subspan(line * pitch, row), staging.subspan(decoded[i].offset + (line * row)).begin());
        }
    }
    SDL_UnmapGPUTransferBuffer(device, transfer.get());

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        return std::unexpected{ core::Error{ std::format("no command buffer for the sprite atlas: {}", SDL_GetError()) } };
    }
    SDL_PushGPUDebugGroup(commands, "load sprite atlas");
    ClearPages(commands, array.texture.get());
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    for (std::size_t i = 0; i < decoded.size(); ++i) {
        const core::atlas::Sprite& sprite = core::atlas::SPRITES.at(i);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer.get();
        source.offset = decoded[i].offset;
        source.pixels_per_row = sprite.width;
        source.rows_per_layer = sprite.height;
        SDL_GPUTextureRegion region{};
        region.texture = array.texture.get();
        region.layer = sprite.page;
        region.x = sprite.x;
        region.y = sprite.y;
        region.w = sprite.width;
        region.h = sprite.height;
        region.d = 1;
        SDL_UploadToGPUTexture(copy, &source, &region, false); // never cycled: the whole array would be (§8.7)
    }
    SDL_EndGPUCopyPass(copy);
    SDL_GenerateMipmapsForGPUTexture(commands, array.texture.get());
    SDL_PopGPUDebugGroup(commands);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return std::unexpected{ core::Error{ std::format("the sprite atlas upload wasn't submitted: {}", SDL_GetError()) } };
    }
    PN_LOG_INFO(
        "sprite atlas: {} sprites on {} pages of {}x{}, {} MiB of images, decoded and uploaded in {} ms",
        core::atlas::SPRITES.size(),
        core::atlas::PAGES,
        core::atlas::PAGE_SIZE,
        core::atlas::PAGE_SIZE,
        bytes >> 20U,
        (platform::NowNs() - startNs) / 1'000'000
    );
    return array;
}

} // namespace gfx::internal
