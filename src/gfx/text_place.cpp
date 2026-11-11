#include "gfx/text_place.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/profile.hpp"

namespace gfx {

namespace {

constexpr u32 CHANNEL_BITS = 8;
constexpr u32 CHANNEL_MASK = 0xFFU;
constexpr u32 CHANNELS = 4;
constexpr f32 HALF = 0.5F;

u32 Scaled(u32 colour, f32 opacity) {
    PN_PROFILE_ZONE();
    u32 scaled = 0;
    for (u32 channel = 0; channel < CHANNELS; ++channel) {
        const u32 shift = channel * CHANNEL_BITS;
        const auto value = static_cast<f32>((colour >> shift) & CHANNEL_MASK) * std::clamp(opacity, 0.0F, 1.0F);
        scaled |= static_cast<u32>(std::lround(value)) << shift;
    }
    return scaled;
}

} // namespace

f32 TextPixels(core::TextKind kind, f32 pixelsPerUnit, f32 scale) {
    PN_PROFILE_ZONE();
    if (kind == core::TextKind::Number) {
        return std::max(NUMBER_WORLD_SIZE * pixelsPerUnit, NUMBER_MIN_PIXELS) * scale;
    }
    return (kind == core::TextKind::Caption ? CAPTION_WORLD_SIZE : LABEL_WORLD_SIZE) * pixelsPerUnit * scale;
}

u32 TextColour(const core::TextItem& item, bool outline, bool shadow) {
    PN_PROFILE_ZONE();
    u32 colour = item.colour;
    if (!outline && !shadow && item.kind != core::TextKind::Number) {
        // Light text needs the dark edge to read on the pale ground; without one it turns dark, as opaque as it was.
        constexpr u32 ALPHA_SHIFT = 24;
        colour = Scaled(TEXT_INK, static_cast<f32>(colour >> ALPHA_SHIFT) / static_cast<f32>(CHANNEL_MASK));
    }
    return item.opacity >= 1.0F ? colour : Scaled(colour, item.opacity);
}

core::Vec2 TextAnchor(const core::TextItem& item, const View& view, u32 width, u32 height) {
    PN_PROFILE_ZONE();
    const ClipTransform clip = WorldToClip(view.camera, width, height);
    const auto alpha = static_cast<f32>(view.alpha);
    const core::Vec2 world = item.previous + ((item.current - item.previous) * alpha) + item.offset;
    return {
        .x = ((world.x * clip.scaleX) + clip.offsetX + 1.0F) * HALF * static_cast<f32>(width),
        .y = (1.0F - ((world.y * clip.scaleY) + clip.offsetY)) * HALF * static_cast<f32>(height),
    };
}

core::Vec2 TextTopLeft(core::Vec2 anchor, core::Vec2 size, core::TextAlign align) {
    PN_PROFILE_ZONE();
    return { .x = anchor.x - (size.x * HALF), .y = align == core::TextAlign::BottomCentre ? anchor.y - size.y : anchor.y };
}

void PlaceGlyphs(const Layout& layout, core::Vec2 topLeft, f32 pixels, u32 colour, std::vector<MeshVertex>& out) {
    PN_PROFILE_ZONE();
    const f32 scale = pixels / SDF_BASE_PIXELS;
    constexpr auto PAGE = static_cast<f32>(GLYPH_PAGE_SIZE);
    for (const PlacedGlyph& glyph : layout.glyphs) {
        const f32 left = topLeft.x + (glyph.x * scale);
        const f32 top = topLeft.y + (glyph.y * scale);
        const f32 right = left + (glyph.width * scale);
        const f32 bottom = top + (glyph.height * scale);
        const auto pageLeft = static_cast<f32>(glyph.u);
        const auto pageTop = static_cast<f32>(glyph.v);
        const u16 uLeft = core::PackUnorm16(pageLeft / PAGE);
        const u16 vTop = core::PackUnorm16(pageTop / PAGE);
        const u16 uRight = core::PackUnorm16((pageLeft + glyph.width) / PAGE);
        const u16 vBottom = core::PackUnorm16((pageTop + glyph.height) / PAGE);
        out.push_back({ .x = left, .y = top, .uv = core::PackPair(uLeft, vTop), .colour = colour });
        out.push_back({ .x = right, .y = top, .uv = core::PackPair(uRight, vTop), .colour = colour });
        out.push_back({ .x = left, .y = bottom, .uv = core::PackPair(uLeft, vBottom), .colour = colour });
        out.push_back({ .x = right, .y = bottom, .uv = core::PackPair(uRight, vBottom), .colour = colour });
    }
}

} // namespace gfx
