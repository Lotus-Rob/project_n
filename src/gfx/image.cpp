#include "gfx/image.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/internal/gpu.hpp"
#include "gfx/internal/resources/png.hpp"
#include "gfx/sprite_block.hpp"

namespace gfx {

namespace {

std::string PngPath(const std::filesystem::path& file) {
    const std::u8string utf8 = file.u8string(); // SDL's paths are UTF-8
    return { utf8.begin(), utf8.end() };
}

} // namespace

namespace internal {

std::expected<Surface, core::Error> DecodePng(const std::filesystem::path& file) {
    const std::string path = PngPath(file);
    const Surface loaded{ SDL_LoadPNG(path.c_str()) };
    if (loaded == nullptr) {
        return std::unexpected{ core::Error{ std::format("{} wasn't loaded: {}", path, SDL_GetError()) } };
    }
    Surface rgba{ SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32) };
    if (rgba == nullptr) {
        return std::unexpected{ core::Error{ std::format("{} wasn't converted to RGBA: {}", path, SDL_GetError()) } };
    }
    return rgba;
}

} // namespace internal

std::expected<Image, core::Error> LoadPng(const std::filesystem::path& file) {
    std::expected<internal::Surface, core::Error> rgba = internal::DecodePng(file);
    if (!rgba) {
        return std::unexpected{ std::move(rgba.error()) };
    }
    const SDL_Surface& surface = **rgba;
    const auto width = static_cast<std::size_t>(surface.w);
    const auto height = static_cast<std::size_t>(surface.h);
    const auto pitch = static_cast<std::size_t>(surface.pitch);
    const std::size_t row = width * BYTES_PER_TEXEL;
    Image image{ .width = static_cast<u32>(width),
                 .height = static_cast<u32>(height),
                 .rgba = std::vector<std::byte>(row * height) };
    const std::span<const std::byte> pixels = internal::BytesAt(surface.pixels, pitch * height);
    for (std::size_t line = 0; line < height; ++line) {
        std::ranges::copy(pixels.subspan(line * pitch, row), std::span{ image.rgba }.subspan(line * row).begin());
    }
    return image;
}

std::expected<void, core::Error> SavePng(const Image& image, const std::filesystem::path& file) {
    const std::string path = PngPath(file);
    const internal::Surface surface{
        SDL_CreateSurface(static_cast<i32>(image.width), static_cast<i32>(image.height), SDL_PIXELFORMAT_RGBA32)
    };
    if (surface == nullptr) {
        return std::unexpected{ core::Error{ std::format("no surface for {}: {}", path, SDL_GetError()) } };
    }
    const auto pitch = static_cast<std::size_t>(surface->pitch);
    const std::size_t row = std::size_t{ image.width } * BYTES_PER_TEXEL;
    const std::span<std::byte> pixels = internal::BytesAt(surface->pixels, pitch * image.height);
    for (std::size_t line = 0; line < image.height; ++line) {
        std::ranges::copy(std::span{ image.rgba }.subspan(line * row, row), pixels.subspan(line * pitch).begin());
    }
    if (!SDL_SavePNG(surface.get(), path.c_str())) {
        return std::unexpected{ core::Error{ std::format("{} wasn't saved: {}", path, SDL_GetError()) } };
    }
    return {};
}

std::size_t DifferingPixels(const Image& first, const Image& second, u8 tolerance) {
    const std::size_t pixels = first.rgba.size() / BYTES_PER_TEXEL;
    if (first.width != second.width || first.height != second.height) {
        return pixels;
    }
    std::size_t differing = 0;
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        for (std::size_t channel = 0; channel < BYTES_PER_TEXEL; ++channel) {
            const std::size_t byte = (pixel * BYTES_PER_TEXEL) + channel;
            const i32 difference = std::to_integer<i32>(first.rgba[byte]) - std::to_integer<i32>(second.rgba[byte]);
            if (std::abs(difference) > tolerance) {
                ++differing;
                break;
            }
        }
    }
    return differing;
}

} // namespace gfx
