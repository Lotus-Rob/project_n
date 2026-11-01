#include "platform/paths.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

#include <SDL3/SDL.h>

namespace platform {

namespace {

// SDL's paths are UTF-8; a plain char path would be read in the Windows code page.
std::filesystem::path FromUtf8(std::string_view utf8) {
    return std::filesystem::path{ std::u8string(utf8.begin(), utf8.end()) };
}

} // namespace

std::expected<std::filesystem::path, core::Error> UserDataDirectory() {
    char* pref = SDL_GetPrefPath(nullptr, "project_n");
    if (pref == nullptr) {
        return std::unexpected{ core::Error{ std::format("no per-user data folder: {}", SDL_GetError()) } };
    }
    std::filesystem::path directory = FromUtf8(pref);
    SDL_free(pref);
    return directory;
}

std::expected<std::filesystem::path, core::Error> AssetsDirectory() {
    const char* base = SDL_GetBasePath(); // owned by SDL
    if (base == nullptr) {
        return std::unexpected{ core::Error{ std::format("the executable's folder is unknown: {}", SDL_GetError()) } };
    }
    return FromUtf8(base) / "assets";
}

} // namespace platform
