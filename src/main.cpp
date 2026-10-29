#include "core/cxx23.hpp"

#include <SDL3/SDL.h>

#include <print>

int main(int, char**)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println(stderr, "SDL_Init failed: {}", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("project_n", 1280, 720, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        std::println(stderr, "SDL_CreateWindowAndRenderer failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    std::println("video driver: {}, renderer: {}", SDL_GetCurrentVideoDriver(), SDL_GetRendererName(renderer));

    for (bool running = true; running;) {
        for (SDL_Event event; SDL_PollEvent(&event);) {
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }
        SDL_SetRenderDrawColorFloat(renderer, 0.08f, 0.10f, 0.14f, 1.0f);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
