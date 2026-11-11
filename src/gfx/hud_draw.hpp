#pragma once

/// @file
/// @ingroup gfx
/// What the temporary HUD hands the renderer each frame (the phase 6 chunk 4 spec): lines of text and bars, placed in
/// target pixels, drawn over everything else. `app::Hud` lays them out; the renderer draws the bars through the overlay's
/// sprites (DrawHudBars() in gfx/volley_draw.hpp) and the lines through the glyph cache.

#include "core/types.hpp"
#include "core/vec2.hpp"

#include <span>
#include <string_view>

namespace gfx {

/// How tall the HUD's text is drawn, in target pixels at text scale 1: TextOptions::scale multiplies it.
inline constexpr f32 HUD_PIXELS = 18.0F;

/// One line of the HUD's text, in target pixels: drawn after every other text, left to right, HUD_PIXELS tall times the
/// text scale.
struct HudLine {
    /// What it says; it must stay valid until the Draw() or Capture() it is passed to returns.
    std::string_view text;
    /// Its box's top-left corner, in target pixels from the target's top left.
    core::Vec2 topLeft{};
    /// Its colour, premultiplied and packed as core::UiColours' are.
    u32 colour = 0;
};

/// One of the HUD's bars, in target pixels: its background, and over it from its left a fill `fraction` of its length,
/// drawn over the world and its texts and under the HUD's text.
struct HudBar {
    /// Its top-left corner, in target pixels from the target's top left.
    core::Vec2 topLeft{};
    /// Its width and height, in target pixels.
    core::Vec2 size{};
    /// How much of it the fill covers, from 0 to 1 (clamped; NaN as 0).
    f32 fraction = 0.0F;
    /// The fill's colour, premultiplied and packed as core::UiColours' are.
    u32 fill = 0;
    /// The background's colour, likewise.
    u32 background = 0;
};

/// What the HUD draws in a frame, over everything else: its bars, then its lines. Its room in the renderer's buffers is
/// taken first, so an overflow drops the world's effects and texts, never the HUD.
struct HudDraw {
    /// Its lines of text.
    std::span<const HudLine> lines;
    /// Its bars.
    std::span<const HudBar> bars;
};

} // namespace gfx
