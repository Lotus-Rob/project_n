#include "platform/window.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"

#include <SDL3/SDL.h>

namespace platform {

struct Window::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() {
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
        PN_LOG_INFO("{}SDL stopped", window != nullptr ? "window closed, " : "");
    }

    SDL_Window* window = nullptr;
    WindowState state{};
};

std::expected<Window, core::Error> Window::Open(std::string_view title, i32 width, i32 height) {
    const i32 version = SDL_GetVersion();
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
        return std::unexpected{ core::Error{ std::format("SDL_Init failed: {}", SDL_GetError()) } };
    }
    auto impl = std::make_unique<Impl>(); // from here on, its destructor calls SDL_Quit
    const std::string name{ title };
    impl->window = SDL_CreateWindow(name.c_str(), width, height, SDL_WINDOW_RESIZABLE);
    if (impl->window == nullptr) {
        return std::unexpected{ core::Error{ std::format("SDL_CreateWindow failed: {}", SDL_GetError()) } };
    }
    PN_LOG_INFO("window {}x{}, video driver: {}", width, height, SDL_GetCurrentVideoDriver());
    return Window{ std::move(impl) };
}

Window::Window(std::unique_ptr<Impl> impl)
  : m_impl{ std::move(impl) } {
}
Window::Window(Window&& other) noexcept = default;
Window::~Window() = default;

const WindowState& Window::Poll(u64 hiddenWaitNs) {
    PN_PROFILE_ZONE();
    WindowState& state = m_impl->state;
    SDL_Event event;
    bool pending =
        state.visible ? SDL_PollEvent(&event) : SDL_WaitEventTimeout(&event, static_cast<i32>(hiddenWaitNs / SDL_NS_PER_MS));
    for (; pending; pending = SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                state.quitRequested = true;
                break;
            case SDL_EVENT_KEY_DOWN:
                state.quitRequested = state.quitRequested || event.key.key == SDLK_ESCAPE;
                break;
            case SDL_EVENT_WINDOW_HIDDEN:
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_OCCLUDED:
                state.visible = false;
                break;
            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_EXPOSED:
                state.visible = true;
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                state.focused = true;
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                state.focused = false;
                break;
            default:
                break;
        }
    }
    return state;
}

void* Window::Handle() const {
    return m_impl->window;
}

} // namespace platform
