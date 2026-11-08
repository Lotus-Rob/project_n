#include "platform/window.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"

#include <SDL3/SDL.h>

namespace platform {

namespace {

// Keys by their place on the keyboard (scancodes), not their label: WASD stays under the left hand on AZERTY or Dvorak.
struct KeyCode {
    SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
    InputCode code = InputCode::KeyW;
};

constexpr std::array KEYS{
    KeyCode{ .key = SDL_SCANCODE_W, .code = InputCode::KeyW },
    KeyCode{ .key = SDL_SCANCODE_A, .code = InputCode::KeyA },
    KeyCode{ .key = SDL_SCANCODE_S, .code = InputCode::KeyS },
    KeyCode{ .key = SDL_SCANCODE_D, .code = InputCode::KeyD },
    KeyCode{ .key = SDL_SCANCODE_UP, .code = InputCode::KeyUp },
    KeyCode{ .key = SDL_SCANCODE_LEFT, .code = InputCode::KeyLeft },
    KeyCode{ .key = SDL_SCANCODE_DOWN, .code = InputCode::KeyDown },
    KeyCode{ .key = SDL_SCANCODE_RIGHT, .code = InputCode::KeyRight },
    KeyCode{ .key = SDL_SCANCODE_ESCAPE, .code = InputCode::KeyEscape },
    KeyCode{ .key = SDL_SCANCODE_M, .code = InputCode::KeyM },
    KeyCode{ .key = SDL_SCANCODE_F5, .code = InputCode::KeyF5 },
    KeyCode{ .key = SDL_SCANCODE_F6, .code = InputCode::KeyF6 },
    KeyCode{ .key = SDL_SCANCODE_F9, .code = InputCode::KeyF9 },
    KeyCode{ .key = SDL_SCANCODE_F10, .code = InputCode::KeyF10 },
};

GamepadFamily FamilyOf(SDL_JoystickID gamepad) {
    const SDL_GamepadType type = SDL_GetGamepadTypeForID(gamepad);
    if (type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE) {
        return GamepadFamily::Xbox;
    }
    if (type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5) {
        return GamepadFamily::PlayStation;
    }
    if (type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
        type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR) {
        return GamepadFamily::Nintendo;
    }
    return GamepadFamily::Other;
}

std::optional<InputEvent> KeyInput(const SDL_KeyboardEvent& key) {
    const std::span keys{ KEYS }; // a span's iterator is a class everywhere, unlike std::array's
    const auto found = std::ranges::find(keys, key.scancode, &KeyCode::key);
    if (key.repeat || found == keys.end()) {
        return std::nullopt;
    }
    return InputEvent{ .code = found->code, .pressed = key.down, .value = key.down ? 1.0F : 0.0F };
}

std::optional<InputEvent> MouseButtonInput(const SDL_MouseButtonEvent& button) {
    if (button.button != SDL_BUTTON_LEFT) {
        return std::nullopt;
    }
    return InputEvent{ .device = Device::Mouse,
                       .code = InputCode::MouseLeft,
                       .pressed = button.down,
                       .value = button.down ? 1.0F : 0.0F,
                       .position = { .x = button.x, .y = button.y } };
}

InputEvent WheelInput(const SDL_MouseWheelEvent& wheel) {
    const f32 notches = wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -wheel.y : wheel.y;
    return InputEvent{ .device = Device::Mouse,
                       .code = InputCode::MouseWheel,
                       .value = notches,
                       .position = { .x = wheel.mouse_x, .y = wheel.mouse_y } };
}

std::optional<InputEvent> AxisInput(const SDL_GamepadAxisEvent& axis) {
    if (axis.axis != SDL_GAMEPAD_AXIS_LEFTX && axis.axis != SDL_GAMEPAD_AXIS_LEFTY) {
        return std::nullopt;
    }
    constexpr f32 AXIS_MAX = 32'767.0F;
    return InputEvent{ .device = Device::Gamepad,
                       .family = FamilyOf(axis.which),
                       .code = axis.axis == SDL_GAMEPAD_AXIS_LEFTX ? InputCode::PadLeftX : InputCode::PadLeftY,
                       .value = std::clamp(static_cast<f32>(axis.value) / AXIS_MAX, -1.0F, 1.0F) };
}

std::optional<InputEvent> PadButtonInput(const SDL_GamepadButtonEvent& button) {
    if (button.button != SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && button.button != SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
        return std::nullopt;
    }
    return InputEvent{ .device = Device::Gamepad,
                       .family = FamilyOf(button.which),
                       .code = button.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER ? InputCode::PadLeftShoulder
                                                                                 : InputCode::PadRightShoulder,
                       .pressed = button.down,
                       .value = button.down ? 1.0F : 0.0F };
}

// The player's input in `event`, if it is one the game knows.
std::optional<InputEvent> InputOf(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
        return KeyInput(event.key);
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        return MouseButtonInput(event.button);
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        return WheelInput(event.wheel);
    }
    if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        return AxisInput(event.gaxis);
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        return PadButtonInput(event.gbutton);
    }
    return std::nullopt;
}

} // namespace

struct Window::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() {
        for (SDL_Gamepad* gamepad : gamepads) {
            SDL_CloseGamepad(gamepad);
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
        PN_LOG_INFO("{}SDL stopped", window != nullptr ? "window closed, " : "");
    }

    SDL_Window* window = nullptr;
    WindowState state{};
    std::vector<SDL_Gamepad*> gamepads{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer

    void OpenGamepad(SDL_JoystickID which) {
        SDL_Gamepad* gamepad = SDL_OpenGamepad(which);
        if (gamepad == nullptr) {
            PN_LOG_WARNING("a gamepad was connected but couldn't be opened: {}", SDL_GetError());
            return;
        }
        gamepads.push_back(gamepad);
        const char* name = SDL_GetGamepadName(gamepad);
        PN_LOG_INFO("gamepad connected: {}", name != nullptr ? name : "unnamed");
    }

    void CloseGamepad(SDL_JoystickID which) {
        SDL_Gamepad* gamepad = SDL_GetGamepadFromID(which);
        if (std::erase(gamepads, gamepad) > 0) {
            SDL_CloseGamepad(gamepad);
            PN_LOG_INFO("gamepad disconnected");
        }
    }
};

std::expected<Window, core::Error> Window::Open(std::string_view title, i32 width, i32 height, bool fullscreen) {
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
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        PN_LOG_WARNING("no gamepads: SDL's gamepad support didn't start: {}", SDL_GetError());
    }
    const std::string name{ title };
    impl->window =
        SDL_CreateWindow(name.c_str(), width, height, SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    if (impl->window == nullptr) {
        return std::unexpected{ core::Error{ std::format("SDL_CreateWindow failed: {}", SDL_GetError()) } };
    }
    PN_LOG_INFO(
        "window {}x{}{}, video driver: {}", width, height, fullscreen ? " (fullscreen)" : "", SDL_GetCurrentVideoDriver()
    );
    // Fullscreen takes the screen's size, not the one asked for.
    SDL_GetWindowSize(impl->window, &impl->state.width, &impl->state.height);
    return Window{ std::move(impl) };
}

Window::Window(std::unique_ptr<Impl> impl)
  : m_impl{ std::move(impl) } {
}
Window::Window(Window&& other) noexcept = default;
Window::~Window() = default;

const WindowState& Window::Poll(u64 hiddenWaitNs, std::vector<InputEvent>& inputs) {
    PN_PROFILE_ZONE();
    WindowState& state = m_impl->state;
    inputs.clear();
    SDL_Event event;
    bool pending =
        state.visible ? SDL_PollEvent(&event) : SDL_WaitEventTimeout(&event, static_cast<i32>(hiddenWaitNs / SDL_NS_PER_MS));
    for (; pending; pending = SDL_PollEvent(&event)) {
        if (const std::optional<InputEvent> input = InputOf(event)) {
            inputs.push_back(*input);
            continue;
        }
        switch (event.type) {
            case SDL_EVENT_QUIT:
                state.quitRequested = true;
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                m_impl->OpenGamepad(event.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                m_impl->CloseGamepad(event.gdevice.which);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                state.width = event.window.data1;
                state.height = event.window.data2;
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
                state.minimised = true;
                state.visible = false;
                break;
            case SDL_EVENT_WINDOW_RESTORED:
                state.minimised = false;
                state.visible = true;
                break;
            case SDL_EVENT_WINDOW_HIDDEN:
            case SDL_EVENT_WINDOW_OCCLUDED:
                state.visible = false;
                break;
            case SDL_EVENT_WINDOW_SHOWN:
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

void ShowError(std::string_view title, std::string_view message) {
    const std::string titleText{ title };
    const std::string messageText{ message };
    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, titleText.c_str(), messageText.c_str(), nullptr)) {
        PN_LOG_WARNING("the error couldn't be shown in a message box: {}", SDL_GetError());
    }
}

void ReportStartFailure(
    std::string_view what,
    const core::Error& error,
    const std::function<void(std::string_view, std::string_view)>& show
) {
    PN_LOG_CRITICAL("{}: {}", what, error.message);
    show("project_n can't start", std::format("{}:\n{}", what, error.message));
}

std::optional<core::Vec2> Window::Cursor() const {
    PN_PROFILE_ZONE();
    if (SDL_GetMouseFocus() != m_impl->window) {
        return std::nullopt;
    }
    core::Vec2 cursor{};
    static_cast<void>(SDL_GetMouseState(&cursor.x, &cursor.y));
    return cursor;
}

void* Window::Handle() const {
    return m_impl->window;
}

} // namespace platform
