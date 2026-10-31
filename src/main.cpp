#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"

#include <SDL3/SDL.h>

namespace {

constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;
constexpr SDL_FColor CLEAR_COLOR{ .r = 0.08F, .g = 0.10F, .b = 0.14F, .a = 1.0F };

// The per-user data folder, so a game installed in a read-only folder can still write its logs.
std::filesystem::path LogDirectory() {
    char* pref = SDL_GetPrefPath(nullptr, "project_n");
    if (pref == nullptr) {
        return {};
    }
    // SDL's paths are UTF-8; a plain char path would be read in the Windows code page.
    const std::string_view utf8{ pref };
    std::filesystem::path directory = std::filesystem::path{ std::u8string(utf8.begin(), utf8.end()) } / "logs";
    SDL_free(pref);
    return directory;
}

// A switch over SDL_LogPriority can't satisfy both -Wswitch-default and -Wcovered-switch-default;
// the priorities are ordered, so compare instead.
void SDLCALL LogFromSdl(void* /*userdata*/, int /*category*/, SDL_LogPriority priority, const char* message) noexcept {
    if (priority >= SDL_LOG_PRIORITY_CRITICAL) {
        PN_LOG_CRITICAL("SDL: {}", message);
    }
    else if (priority >= SDL_LOG_PRIORITY_ERROR) {
        PN_LOG_ERROR("SDL: {}", message);
    }
    else if (priority >= SDL_LOG_PRIORITY_WARN) {
        PN_LOG_WARNING("SDL: {}", message);
    }
    else if (priority >= SDL_LOG_PRIORITY_INFO) {
        PN_LOG_INFO("SDL: {}", message);
    }
    else {
        PN_LOG_DEBUG("SDL: {}", message);
    }
}

int Run() {
    SDL_SetLogOutputFunction(LogFromSdl, nullptr);
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_DEBUG);
    const int version = SDL_GetVersion();
    PN_LOG_INFO(
        "SDL {}.{}.{} on {}",
        SDL_VERSIONNUM_MAJOR(version),
        SDL_VERSIONNUM_MINOR(version),
        SDL_VERSIONNUM_MICRO(version),
        SDL_GetPlatform()
    );
#ifdef SDL_PLATFORM_LINUX
    // SDL picks XWayland over Wayland when the compositor lacks fifo-v1 (niri, for one).
    // A normal-priority hint, so SDL_VIDEO_DRIVER in the environment still overrides it.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland,x11");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        PN_LOG_ERROR("SDL_Init failed: {}", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("project_n", WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        PN_LOG_ERROR("SDL_CreateWindowAndRenderer failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    PN_LOG_INFO(
        "window {}x{}, video driver: {}, renderer: {}",
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_GetCurrentVideoDriver(),
        SDL_GetRendererName(renderer)
    );
    // SDL3 defaults to off: the loop would spin a core and the GPU at 100%.
    if (SDL_SetRenderVSync(renderer, 1)) {
        PN_LOG_DEBUG("vsync on");
    }
    else {
        PN_LOG_WARNING("vsync unavailable, so the frame rate is uncapped: {}", SDL_GetError());
    }

    for (bool running = true; running;) {
        for (SDL_Event event; SDL_PollEvent(&event);) {
            if (event.type == SDL_EVENT_QUIT) {
                PN_LOG_DEBUG("quit requested");
                running = false;
            }
            else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                PN_LOG_DEBUG("Escape pressed");
                running = false;
            }
        }
        SDL_SetRenderDrawColorFloat(renderer, CLEAR_COLOR.r, CLEAR_COLOR.g, CLEAR_COLOR.b, CLEAR_COLOR.a);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        PN_PROFILE_FRAME();
    }

    PN_LOG_INFO("shutting down");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace

int main(int /*argc*/, char** /*argv*/) {
    const std::filesystem::path logs = LogDirectory();
    core::log::Start(logs);
    if (logs.empty()) {
        PN_LOG_WARNING("no per-user data folder, so logs go to the console only: {}", SDL_GetError());
    }
    PN_LOG_NOTICE("project_n starting");
    try {
        const int status = Run();
        PN_LOG_NOTICE("project_n stopped with status {}", status);
        return status;
    }
    catch (const std::exception& e) {
        PN_LOG_CRITICAL("fatal: {}", e.what());
        return 1;
    }
    catch (...) {
        PN_LOG_CRITICAL("fatal: an exception of unknown type");
        return 1;
    }
}
