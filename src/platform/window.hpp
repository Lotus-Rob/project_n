#pragma once

/// @file
/// @ingroup platform
/// The game's window and its events.

#include "core/error.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace platform {

/// A display and its current mode, as the log names it.
struct DisplayMode {
    /// SDL's ID for the display; 0 for none.
    u32 id = 0;
    /// The name the system gives it (a connector such as `DP-2` on Linux); empty when it has none.
    std::string name{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// Its width in pixels.
    i32 width = 0;
    /// Its height in pixels.
    i32 height = 0;
    /// Its refresh rate exactly, in hertz, as this over refreshDenominator; 0 when unknown.
    i32 refreshNumerator = 0;
    /// The refresh rate's denominator; 0 when unknown.
    i32 refreshDenominator = 0;

    /// Its refresh rate in hertz; 0 when unknown.
    [[nodiscard]] f64 RefreshHz() const {
        return refreshDenominator == 0 ? 0.0 : static_cast<f64>(refreshNumerator) / refreshDenominator;
    }

    /// Whether both are the same display in the same mode, exactly.
    bool operator==(const DisplayMode&) const = default;
};

/// What the window's events have said so far.
struct WindowState {
    /// The player asked to quit (closed the window).
    bool quitRequested = false;
    /// The window can be seen: neither hidden nor minimised. Nothing is drawn while it can't.
    bool visible = true;
    /// The window has the keyboard focus.
    bool focused = true;
    /// The window is minimised (unlike `visible`, not merely covered by another window).
    bool minimised = false;
    /// The window's width in window coordinates, the ones InputEvent::position uses.
    i32 width = 0;
    /// Its height, likewise.
    i32 height = 0;
    /// The display the window is on, read when it opens and again whenever SDL says it was shown, went fullscreen,
    /// moved to another display or that display's mode changed: a compositor may place a new window on another display
    /// than the one SDL opened it on.
    DisplayMode display{};
};

/// The log line naming `mode`, `display: WxH at N Hz (name)`, which `project_n measure` reads to judge whether a run's
/// frames were paced by the display.
[[nodiscard]] std::string DisplayLine(const DisplayMode& mode);

/// Records that the window is now on `now`, as Window::Open and Poll do: returns whether that differs from
/// `state.display` (another display, or the same one's mode changed), which is when the log names it again.
bool MoveToDisplay(WindowState& state, const DisplayMode& now);

/// The kind of device an input came from.
enum class Device : u8 {
    Keyboard, ///< A key.
    Mouse,    ///< A mouse button or the wheel.
    Gamepad,  ///< A gamepad's button or stick.
};

/// The family a gamepad belongs to, which decides how its buttons are drawn in prompts.
enum class GamepadFamily : u8 {
    Other,       ///< Not a gamepad, or one of no known family: the Xbox layout's names.
    Xbox,        ///< Xbox controllers and their copies.
    PlayStation, ///< PlayStation controllers.
    Nintendo,    ///< Nintendo controllers.
};

/// A physical input the game knows. SDL's standard gamepad layout gives every controller the same
/// buttons and sticks. Only the inputs the default bindings use; rebinding adds the rest.
enum class InputCode : u8 {
    KeyW,
    KeyA,
    KeyS,
    KeyD,
    KeyUp,
    KeyLeft,
    KeyDown,
    KeyRight,
    KeyEscape,
    KeyM,
    KeyF2,
    KeyF5,
    KeyF6,
    KeyF7,
    KeyF8,
    KeyF9,
    KeyF10,
    MouseLeft,
    MouseWheel,       ///< `value`: notches turned, positive away from the player.
    PadLeftX,         ///< `value`: -1 (left) to 1 (right).
    PadLeftY,         ///< `value`: -1 (up) to 1 (down).
    PadLeftShoulder,  ///< The left bumper.
    PadRightShoulder, ///< The right bumper.
    MouseRight,
    KeySpace,
    Key1,
    Key2,
    Key3,
    Key4,
    Key5,
    MouseMiddle,   ///< The middle button (the wheel's click).
    PadRightX,     ///< `value`: -1 (left) to 1 (right).
    PadRightY,     ///< `value`: -1 (up) to 1 (down).
    PadRightStick, ///< The right stick's click.
    KeyI,
    KeyJ,
    KeyK,
    KeyL,
    KeyC,
    KeyMinus,  ///< The key right of 0 on a US keyboard, by its place.
    KeyEquals, ///< The key right of KeyMinus, by its place.
};

/// One input from the player: a button pressed or released, a stick moved, the wheel turned. Key
/// repeats are dropped.
struct InputEvent {
    /// The device it came from.
    Device device = Device::Keyboard;
    /// For a gamepad, its family; Other for the rest.
    GamepadFamily family = GamepadFamily::Other;
    /// Which input.
    InputCode code = InputCode::KeyW;
    /// Whether a button went down (false: up). Unused for sticks and the wheel.
    bool pressed = false;
    /// A stick's position or the wheel's notches; 1 for a pressed button, 0 for a released one.
    f32 value = 0.0F;
    /// For a mouse event, the pointer in window coordinates (WindowState's width and height).
    core::Vec2 position{};
    /// For a mouse button, whether Alt was held as it was pressed or released.
    bool alt = false;
};

/// Whether the window's state pauses the game: it lost the focus or was minimised.
[[nodiscard]] constexpr bool PausesGame(const WindowState& state) {
    return !state.focused || state.minimised;
}

/// Shows `message` in a message box titled `title` and returns once the player closes it: for a
/// failure a player who started the game from the desktop would otherwise never see. Logs why if
/// it can't be shown (with no display, say).
void ShowError(std::string_view title, std::string_view message);

/// Reports that the game can't start because `what` failed for `error`: logs it at Critical and
/// shows it with `show` (a title, then the message), since a player who started the game from the
/// desktop sees no console.
void ReportStartFailure(
    std::string_view what,
    const core::Error& error,
    const std::function<void(std::string_view, std::string_view)>& show = ShowError
);

/// The game's one window. Opening it starts SDL's video; closing it (destruction) stops SDL.
class Window {
public:
    /// Opens a resizable window of `width` by `height` titled `title`, covering the screen when
    /// `fullscreen`, or says why it couldn't.
    [[nodiscard]] static std::expected<Window, core::Error> Open(
        std::string_view title,
        i32 width,
        i32 height,
        bool fullscreen = false
    );

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    /// Takes over `other`'s window, leaving `other` empty.
    Window(Window&& other) noexcept;
    // No move assignment: closing the replaced window would stop SDL under the one moved in.
    Window& operator=(Window&& other) = delete;
    ~Window();

    /// Handles the events that arrived since the last call, replacing `inputs` with the player's
    /// input among them, in order. While the window can't be seen, waits up to `hiddenWaitNs` for
    /// the first one instead of returning at once, so a hidden game doesn't spin. Returns the
    /// window's state from every event so far, not only this call's (`quitRequested` stays set);
    /// the reference stays valid while this Window exists, and the next Poll() updates it in place.
    /// Gamepads open as they are connected and close as they go.
    const WindowState& Poll(u64 hiddenWaitNs, std::vector<InputEvent>& inputs);

    /// Where the mouse cursor is over the window, in the coordinates of WindowState's size and of
    /// InputEvent::position; empty while the cursor is elsewhere.
    [[nodiscard]] std::optional<core::Vec2> Cursor() const;

    /// The window's SDL_Window, for gfx to present to; opaque so SDL stays out of headers.
    [[nodiscard]] void* Handle() const;

private:
    struct Impl;
    explicit Window(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace platform
