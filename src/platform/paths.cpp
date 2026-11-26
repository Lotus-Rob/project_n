#include "platform/paths.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/path_text.hpp"

#include <SDL3/SDL.h>

namespace platform {

std::expected<std::filesystem::path, core::Error> UserDataDirectory() {
    char* pref = SDL_GetPrefPath(nullptr, "project_n");
    if (pref == nullptr) {
        return std::unexpected{ core::Error{ std::format("no per-user data folder: {}", SDL_GetError()) } };
    }
    std::filesystem::path directory = core::PathOf(pref);
    SDL_free(pref);
    return directory;
}

std::expected<std::filesystem::path, core::Error> AssetsDirectory() {
    const char* base = SDL_GetBasePath(); // owned by SDL
    if (base == nullptr) {
        return std::unexpected{ core::Error{ std::format("the executable's folder is unknown: {}", SDL_GetError()) } };
    }
    return core::PathOf(base) / "assets";
}

} // namespace platform
