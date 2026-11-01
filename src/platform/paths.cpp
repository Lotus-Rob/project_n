#include "platform/paths.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

#include <SDL3/SDL.h>

namespace platform {

std::expected<std::filesystem::path, core::Error> UserDataDirectory() {
    char* pref = SDL_GetPrefPath(nullptr, "project_n");
    if (pref == nullptr) {
        return std::unexpected{ core::Error{ std::format("no per-user data folder: {}", SDL_GetError()) } };
    }
    // SDL's paths are UTF-8; a plain char path would be read in the Windows code page.
    const std::string_view utf8{ pref };
    std::filesystem::path directory{ std::u8string(utf8.begin(), utf8.end()) };
    SDL_free(pref);
    return directory;
}

} // namespace platform
