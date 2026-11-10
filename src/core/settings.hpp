#pragma once

/// @file
/// @ingroup core
/// The player's settings (design doc 0002.3 §7): what `settings.yaml` in the per-user data folder holds.

#include "core/strings.hpp"
#include "core/types.hpp"

namespace core {

/// The window's width when no settings say otherwise.
inline constexpr i32 DEFAULT_WINDOW_WIDTH = 1280;
/// The window's height when no settings say otherwise.
inline constexpr i32 DEFAULT_WINDOW_HEIGHT = 720;

/// The player's settings; a default-built one holds the defaults.
struct Settings {
    /// The window's width in pixels when not fullscreen.
    i32 windowWidth = DEFAULT_WINDOW_WIDTH;
    /// The window's height in pixels when not fullscreen.
    i32 windowHeight = DEFAULT_WINDOW_HEIGHT;
    /// Whether the window covers the whole screen.
    bool fullscreen = false;
    /// The language the game's strings show in.
    Language language = Language::English;
};

} // namespace core
