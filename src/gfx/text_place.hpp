#pragma once

/// @file
/// @ingroup gfx
/// Where game text goes on the target and how large (the phase 6 chunk 3 spec, "Drawing: crisp at every size"): each
/// kind of text has a size in world units, drawn at that size times the pixels a world unit covers (PixelsPerUnit():
/// the window's fit and the zoom), so it keeps its proportion to the world at every window size, and its glyphs keep sub-pixel
/// positions, never rounded, so moving text glides. Numbers, thousands at once, are composed from their characters'
/// layouts and placed on the job system (PlaceNumbers()). Pure, tested without a device.

#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/stable_id.hpp"
#include "core/types.hpp"
#include "core/ui_colours.hpp"
#include "core/vec2.hpp"
#include "gfx/camera.hpp"
#include "gfx/mesh_vertex.hpp"
#include "gfx/text_layout.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace gfx {

/// A label's size (its font's pixel size where a world unit is a pixel: zoom 1 at 1920x1080) in world units.
inline constexpr f32 LABEL_WORLD_SIZE = 20.0F;
/// A caption's size in world units.
inline constexpr f32 CAPTION_WORLD_SIZE = 32.0F;
/// A number's size in world units.
inline constexpr f32 NUMBER_WORLD_SIZE = 24.0F;
/// The smallest a label reads at, in screen pixels at text scale 1: below it, a label fades out.
inline constexpr f32 LABEL_MIN_PIXELS = 9.0F;
/// The smallest a number is drawn, in screen pixels at text scale 1: zoomed further out, it stops shrinking.
inline constexpr f32 NUMBER_MIN_PIXELS = 12.0F;
/// The height on the screen, in pixels, below which a creature is too small to see (TooSmallToSee()): its damage numbers
/// start from where its label, or the stack it joined, is placed (faded out or not) instead of their own anchors.
inline constexpr f32 VISIBLE_CREATURE_PIXELS = 8.0F;

/// The pixel size a text of `kind` is drawn at where a world unit covers `pixelsPerUnit` target pixels
/// (PixelsPerUnit()) and the text is scaled by `scale` (the text size setting, 1 by default): its world size ×
/// pixelsPerUnit × scale. A number never goes below NUMBER_MIN_PIXELS × scale; labels and captions don't stop shrinking
/// (a label too small to read fades out instead).
[[nodiscard]] f32 TextPixels(core::TextKind kind, f32 pixelsPerUnit, f32 scale);

/// Whether a creature `entityPixels` tall on the screen (LabelInput::entityPixels) is too small to see: under
/// VISIBLE_CREATURE_PIXELS, and not 0, which says its height is unknown (core::TextItem::height unset).
[[nodiscard]] bool TooSmallToSee(f32 entityPixels);

/// The colour `item` is drawn in, scaled by its opacity (premultiplied, so every channel): a label's and a caption's
/// from `colours` (core::UiColour::Label, Caption), and, when the outline and the shadow are both off, core::UiColour::
/// TextInk as opaque as that colour, so it reads on the pale ground. A number keeps its own colour: its kind's, which the
/// damage numbers take from the table, the dark one when both are off.
[[nodiscard]] u32 TextColour(const core::TextItem& item, const core::UiColours& colours, bool outline, bool shadow);

/// Where `item`'s anchor, moved by its offset, falls on a target `width` by `height` pixels as `view` shows it, in
/// target pixels from the top left: interpolated by the view's alpha and never rounded.
[[nodiscard]] core::Vec2 TextAnchor(const core::TextItem& item, const View& view, u32 width, u32 height);

/// TextAnchor() with the view's world to clip transform `clip` (WorldToClip()) and alpha `alpha` already worked out: for
/// a frame's texts, which share them.
[[nodiscard]] core::Vec2 TextAnchor(const core::TextItem& item, const ClipTransform& clip, f32 alpha, u32 width, u32 height);

/// The top-left corner, in target pixels, of a text box `size` pixels large whose `align` point is at `anchor`.
[[nodiscard]] core::Vec2 TextTopLeft(core::Vec2 anchor, core::Vec2 size, core::TextAlign align);

/// Appends a quad per glyph of `layout` to `out`, in its order: the layout drawn at `pixels` (each base pixel
/// `pixels / SDF_BASE_PIXELS` target pixels) with its box's top left at `topLeft`, in `colour`. Positions are kept
/// exactly, sub-pixel and all; the texture coordinates cover each glyph's field on its page.
void PlaceGlyphs(const Layout& layout, core::Vec2 topLeft, f32 pixels, u32 colour, std::vector<MeshVertex>& out);

/// The characters numbers are composed from (ComposeNumber()), in NumberFace's order: the digits, then the signs.
inline constexpr std::string_view NUMBER_FACE_CHARACTERS = "0123456789+-";
/// The most characters a number shows: a "+", a "-" and the nineteen digits of the largest i64.
inline constexpr std::size_t NUMBER_MAX_CHARACTERS = 21;
/// How many texts each job chunk of PlaceNumbers() places. Fixed, so the vertices are the same bytes whatever the
/// worker count.
inline constexpr u32 NUMBER_CHUNK = 256;

/// What numbers are composed from: each of NUMBER_FACE_CHARACTERS laid out alone, left to right. The font's digits
/// are tabular and it kerns none of them with the signs, so a number composed from them character by character is the
/// layout TextLayout::Lay() shapes from its text (a test pins it), with no string and no layout looked up per number.
struct NumberFace {
    /// Each character's layout, in NUMBER_FACE_CHARACTERS' order, each one glyph at most. They hold until the glyph
    /// cache's next EndFrame().
    std::array<const Layout*, NUMBER_FACE_CHARACTERS.size()> characters{};
};

/// The face of this frame's numbers: each of NUMBER_FACE_CHARACTERS laid out alone by `layout` (TextLayout::Lay()).
[[nodiscard]] NumberFace NumberFaceOf(TextLayout& layout);

/// A number laid out from a NumberFace, in base pixels as a Layout is, without allocating.
struct ComposedNumber {
    /// The width of its box.
    f32 width = 0.0F;
    /// Its height.
    f32 height = 0.0F;
    /// Its glyphs that draw anything and are rasterised, in drawing order: the first `count`.
    std::array<PlacedGlyph, NUMBER_MAX_CHARACTERS> glyphs{};
    /// How many of `glyphs` it has.
    u32 count = 0;
    /// Whether every glyph is there (Layout::complete).
    bool complete = true;
};

/// `value` laid out across from `face` as the renderer writes it: a "+" first when `sign` is Plus, then the value in
/// decimal, a "-" first when it's negative. Each character's glyph is its layout's, moved right by the advances of the
/// characters before it (each layout's width less its margins); the box is as tall as theirs. The layout
/// TextLayout::Lay() shapes from that text, glyph for glyph.
[[nodiscard]] ComposedNumber ComposeNumber(const NumberFace& face, i64 value, core::NumberSign sign);

/// Where the numbers of a labelled creature too small to see (TooSmallToSee()) start instead of their own anchors: the
/// middle of its label's box's top edge, where the label pass placed it.
struct NumberOrigin {
    /// The creature.
    core::StableId entity{};
    /// Where its numbers start, in target pixels from the top left.
    core::Vec2 start{};
};

/// What a frame's numbers are placed with (PlaceNumbers()).
struct NumberPlacement {
    /// What they're composed from.
    NumberFace face{};
    /// The view's world to clip transform on the target (WorldToClip()).
    ClipTransform clip{};
    /// The view's alpha (View::alpha).
    f32 alpha = 1.0F;
    /// The target's width in pixels.
    u32 width = 0;
    /// Its height.
    u32 height = 0;
    /// The size numbers are drawn at, in pixels (TextPixels()).
    f32 pixels = 0.0F;
    /// The creatures whose numbers start elsewhere, sorted by entity.
    std::span<const NumberOrigin> origins;
};

/// Places the numbers of `texts` that run left to right and show (opacity above 0), skipping every other text: appends
/// a quad per glyph to `vertices` and each quad's glyph page to `pages`, in their order. Each is composed
/// (ComposeNumber()) and drawn at `placement.pixels` in its colour (TextColour()), its box's `align` point at its anchor
/// (TextAnchor()), or its creature's origin when it has one, moved by its scatter and risen by its lift (both in ems):
/// the quads PlaceGlyphs() gives for its shaped layout there (TextTopLeft()). Runs on `jobs` in chunks of NUMBER_CHUNK
/// texts, each writing its own part of the output: the same bytes whatever the worker count. `firstQuads` is scratch the
/// caller keeps from call to call (its contents are overwritten), so a frame allocates nothing once it has grown.
void PlaceNumbers(
    std::span<const core::TextItem> texts,
    const NumberPlacement& placement,
    core::Jobs& jobs,
    std::vector<MeshVertex>& vertices,
    std::vector<u32>& pages,
    std::vector<std::size_t>& firstQuads
);

} // namespace gfx
