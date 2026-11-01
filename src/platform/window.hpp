#pragma once

/// @file
/// @ingroup platform
/// The game's window and its events.

#include "core/error.hpp"
#include "core/types.hpp"

#include <expected>
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
};

/// An opaque colour in the window's colour space.
struct Colour {
    /// The red channel, from 0 (none) to 1 (full).
    f32 red = 0.0F;
    /// The green channel, from 0 (none) to 1 (full).
    f32 green = 0.0F;
    /// The blue channel, from 0 (none) to 1 (full).
    f32 blue = 0.0F;
};

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

    /// Fills the window with `colour` and shows it, without waiting for vsync.
    // shortcut: draws through SDL_Renderer; the gfx module replaces it with SDL_GPU in phase 2 of
    // design doc 0002 §16.
    void Clear(Colour colour);

private:
    struct Impl;
    explicit Window(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace platform
