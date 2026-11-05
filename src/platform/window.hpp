#pragma once

/// @file
/// @ingroup platform
/// The game's window and its events.

#include "core/error.hpp"
#include "core/types.hpp"

#include <expected>
#include <functional>
#include <memory>
#include <string_view>

namespace platform {

/// What the window's events have said so far.
struct WindowState {
    /// The player asked to quit (closed the window or pressed Escape).
    bool quitRequested = false;
    /// The window can be seen: neither hidden nor minimised. Nothing is drawn while it can't.
    bool visible = true;
    /// The window has the keyboard focus.
    bool focused = true;
    /// The window is minimised (unlike `visible`, not merely covered by another window).
    bool minimised = false;
    /// How far the mouse wheel turned during the last Poll(), in notches; positive is away from the player.
    f32 wheelNotches = 0.0F;
};

/// Whether the window's state pauses the game (design doc 0002 §6): it lost the focus or was minimised.
[[nodiscard]] constexpr bool PausesGame(const WindowState& state) {
    return !state.focused || state.minimised;
}

/// Shows `message` in a message box titled `title` and returns once the player closes it: for a
/// failure a player who started the game from the desktop would otherwise never see. Logs why if
/// it can't be shown (with no display, say).
void ShowError(std::string_view title, std::string_view message);

/// Reports that the game can't start because `what` failed for `error`: logs it at Critical and
/// shows it with `show` (a title, then the message), since a player who started the game from the
/// desktop sees no console (design doc 0002 §12).
void ReportStartFailure(
    std::string_view what,
    const core::Error& error,
    const std::function<void(std::string_view, std::string_view)>& show = ShowError
);

/// The game's one window. Opening it starts SDL's video; closing it (destruction) stops SDL.
class Window {
public:
    /// Opens a resizable window of `width` by `height` titled `title`, or says why it couldn't.
    [[nodiscard]] static std::expected<Window, core::Error> Open(std::string_view title, i32 width, i32 height);

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    /// Takes over `other`'s window, leaving `other` empty.
    Window(Window&& other) noexcept;
    // No move assignment: closing the replaced window would stop SDL under the one moved in.
    Window& operator=(Window&& other) = delete;
    ~Window();

    /// Handles the events that arrived since the last call. While the window can't be seen, waits
    /// up to `hiddenWaitNs` for the first one instead of returning at once, so a hidden game
    /// doesn't spin. Returns the window's state from every event so far, not only this call's
    /// (`quitRequested` stays set); the reference stays valid while this Window exists, and the
    /// next Poll() updates it in place.
    const WindowState& Poll(u64 hiddenWaitNs);

    /// The window's SDL_Window, for gfx to present to; opaque so SDL stays out of headers.
    [[nodiscard]] void* Handle() const;

private:
    struct Impl;
    explicit Window(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace platform
