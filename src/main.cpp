#include "core/cxx23.hpp" // IWYU pragma: keep

#include <SDL3/SDL.h>
#include <tracy/Tracy.hpp>

#include <exception>
#include <print>

namespace {

constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;
constexpr SDL_FColor CLEAR_COLOR{ .r = 0.08F, .g = 0.10F, .b = 0.14F, .a = 1.0F };

int Run() {
#ifdef SDL_PLATFORM_LINUX
    // SDL picks XWayland over Wayland when the compositor lacks fifo-v1 (niri, for one).
    // A normal-priority hint, so SDL_VIDEO_DRIVER in the environment still overrides it.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland,x11");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println(stderr, "SDL_Init failed: {}", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("project_n", WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        std::println(stderr, "SDL_CreateWindowAndRenderer failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1); // SDL3 defaults to off: the loop would spin a core and the GPU at 100%
    std::println("video driver: {}, renderer: {}", SDL_GetCurrentVideoDriver(), SDL_GetRendererName(renderer));

    for (bool running = true; running;) {
        for (SDL_Event event; SDL_PollEvent(&event);) {
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }
        SDL_SetRenderDrawColorFloat(renderer, CLEAR_COLOR.r, CLEAR_COLOR.g, CLEAR_COLOR.b, CLEAR_COLOR.a);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        FrameMark;
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace

int main(int /*argc*/, char** /*argv*/) {
    try {
        return Run();
    }
    catch (const std::exception& e) {
        std::println(stderr, "fatal: {}", e.what());
        return 1;
    }
}
