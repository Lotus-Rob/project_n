#pragma once

/// @file
/// @ingroup gfx
/// Where game text goes on the target and how large (the phase 6 chunk 3 spec, "Drawing: crisp at every size"): each
/// kind of text has a size in world units, drawn at that size times the pixels a world unit covers (PixelsPerUnit():
/// the window's fit and the zoom), so it keeps its proportion to the world at every window size, and its glyphs keep sub-pixel
/// positions, never rounded, so moving text glides. Pure, tested without a device.

#include "core/frame.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "gfx/camera.hpp"
#include "gfx/mesh_vertex.hpp"
#include "gfx/text_layout.hpp"

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

/// The dark ink of labels and captions when the outline and the shadow are both off: sRGB #20242A, linear
/// (0.0144, 0.0176, 0.0232), premultiplied and packed as TextItem::colour. The same ink as the outlines and shadows the
/// field shader draws (`INK` in shaders/mesh.frag.hlsl): change both together.
inline constexpr u32 TEXT_INK = 0xFF06'0404U;

/// The pixel size a text of `kind` is drawn at where a world unit covers `pixelsPerUnit` target pixels
/// (PixelsPerUnit()) and the text is scaled by `scale` (the text size setting, 1 by default): its world size ×
/// pixelsPerUnit × scale. A number never goes below NUMBER_MIN_PIXELS × scale; labels and captions don't stop shrinking
/// (a label too small to read fades out instead).
[[nodiscard]] f32 TextPixels(core::TextKind kind, f32 pixelsPerUnit, f32 scale);

/// The colour `item` is drawn in: its colour scaled by its opacity (premultiplied, so every channel), and, for a label
/// or a caption when the outline and the shadow are both off, TEXT_INK instead, so it reads on the pale ground.
[[nodiscard]] u32 TextColour(const core::TextItem& item, bool outline, bool shadow);

/// Where `item`'s anchor, moved by its offset, falls on a target `width` by `height` pixels as `view` shows it, in
/// target pixels from the top left: interpolated by the view's alpha and never rounded.
[[nodiscard]] core::Vec2 TextAnchor(const core::TextItem& item, const View& view, u32 width, u32 height);

/// The top-left corner, in target pixels, of a text box `size` pixels large whose `align` point is at `anchor`.
[[nodiscard]] core::Vec2 TextTopLeft(core::Vec2 anchor, core::Vec2 size, core::TextAlign align);

/// Appends a quad per glyph of `layout` to `out`, in its order: the layout drawn at `pixels` (each base pixel
/// `pixels / SDF_BASE_PIXELS` target pixels) with its box's top left at `topLeft`, in `colour`. Positions are kept
/// exactly, sub-pixel and all; the texture coordinates cover each glyph's field on its page.
void PlaceGlyphs(const Layout& layout, core::Vec2 topLeft, f32 pixels, u32 colour, std::vector<MeshVertex>& out);

} // namespace gfx
