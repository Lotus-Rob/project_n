#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/instances.hpp"
#include "gfx/text_layout.hpp"
#include "gfx/text_place.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 LIGHT = 0xFFCA'DCE7U; // #F4EFE6, linear
constexpr u32 PAGE_U = 1024;
constexpr u32 PAGE_V = 512;
constexpr f32 GLYPH_WIDTH = 24.0F;
constexpr f32 GLYPH_HEIGHT = 30.0F;
constexpr gfx::PlacedGlyph
    GLYPH{ .x = 4.0F, .y = 2.0F, .width = GLYPH_WIDTH, .height = GLYPH_HEIGHT, .page = 0, .u = PAGE_U, .v = PAGE_V };
constexpr gfx::Layout ONE_GLYPH_BOX{ .width = 32.0F, .height = 34.0F };
constexpr core::Vec2 TOP_LEFT{ .x = 10.25F, .y = 7.5F };
constexpr f32 HALF_BASE = gfx::SDF_BASE_PIXELS / 2.0F; // 24 pixels: the layout at half its base size
// Where the glyph lands at half size from TOP_LEFT: its base pixels halved.
constexpr f32 GLYPH_LEFT = 12.25F;
constexpr f32 GLYPH_RIGHT = 24.25F;
constexpr f32 GLYPH_TOP = 8.5F;
constexpr f32 GLYPH_BOTTOM = 23.5F;
constexpr f32 SECOND_GLYPH_X = 40.0F;

constexpr u32 REFERENCE_WIDTH = 1920;
constexpr u32 REFERENCE_HEIGHT = 1080;
constexpr core::Vec2 FROM{ .x = 100.0F, .y = 200.0F };
constexpr core::Vec2 ONTO{ .x = 108.0F, .y = 200.0F };
constexpr core::Vec2 ABOVE{ .x = 0.0F, .y = -6.0F };
constexpr f64 ALPHA = 0.3;
constexpr core::Vec2 BETWEEN{ .x = 102.4F, .y = 194.0F }; // FROM to ONTO at ALPHA, moved by ABOVE
constexpr f32 DOUBLE = 2.0F;

constexpr core::Vec2 ANCHOR{ .x = 100.5F, .y = 50.25F };
constexpr core::Vec2 BOX{ .x = 40.0F, .y = 20.0F };
constexpr f32 BOX_LEFT = 80.5F;
constexpr f32 BOX_TOP_STANDING = 30.25F;

constexpr u32 SOME_COLOUR = 0xFF80'40C8U;
constexpr u32 SOME_COLOUR_AT_HALF = 0x8040'2064U;
constexpr f32 HALF = 0.5F;

constexpr f32 QUARTER = 0.25F;
constexpr f32 LARGER_TEXT = 1.5F;

TEST_CASE("text grows with the pixels a world unit covers") {
    constexpr f32 LABEL_AT_ONE = 20.0F;
    constexpr f32 LABEL_AT_TWO = 40.0F;
    constexpr f32 LABEL_AT_QUARTER = 5.0F; // below readable: faded later
    constexpr f32 CAPTION_AT_HALF = 16.0F;
    CHECK(gfx::TextPixels(core::TextKind::Label, 1.0F, 1.0F) == doctest::Approx(LABEL_AT_ONE));
    CHECK(gfx::TextPixels(core::TextKind::Label, DOUBLE, 1.0F) == doctest::Approx(LABEL_AT_TWO));
    CHECK(gfx::TextPixels(core::TextKind::Label, QUARTER, 1.0F) == doctest::Approx(LABEL_AT_QUARTER));
    CHECK(gfx::TextPixels(core::TextKind::Caption, HALF, 1.0F) == doctest::Approx(CAPTION_AT_HALF));
}

TEST_CASE("numbers stop shrinking at their minimum, scaled with the text size") {
    constexpr f32 SMALLEST = 12.0F;
    constexpr f32 SMALLEST_LARGER = 18.0F;
    constexpr f32 NUMBER_AT_TWO = 48.0F;
    CHECK(gfx::TextPixels(core::TextKind::Number, QUARTER, 1.0F) == doctest::Approx(SMALLEST));
    CHECK(gfx::TextPixels(core::TextKind::Number, QUARTER, LARGER_TEXT) == doctest::Approx(SMALLEST_LARGER));
    CHECK(gfx::TextPixels(core::TextKind::Number, DOUBLE, 1.0F) == doctest::Approx(NUMBER_AT_TWO));
}

TEST_CASE("glyph quads keep sub-pixel positions") {
    gfx::Layout layout = ONE_GLYPH_BOX;
    layout.glyphs = { GLYPH };
    std::vector<gfx::MeshVertex> quads;
    gfx::PlaceGlyphs(layout, TOP_LEFT, HALF_BASE, LIGHT, quads);
    REQUIRE(quads.size() == gfx::QUAD_VERTICES);
    // Top left, top right, bottom left, bottom right.
    CHECK(quads[0].x == GLYPH_LEFT);
    CHECK(quads[0].y == GLYPH_TOP);
    CHECK(quads[1].x == GLYPH_RIGHT);
    CHECK(quads[1].y == GLYPH_TOP);
    CHECK(quads[2].x == GLYPH_LEFT);
    CHECK(quads[2].y == GLYPH_BOTTOM);
    CHECK(quads[3].x == GLYPH_RIGHT);
    CHECK(quads[3].y == GLYPH_BOTTOM);
    constexpr auto PAGE = static_cast<f32>(gfx::GLYPH_PAGE_SIZE);
    const u16 uLeft = core::PackUnorm16(static_cast<f32>(PAGE_U) / PAGE);
    const u16 vTop = core::PackUnorm16(static_cast<f32>(PAGE_V) / PAGE);
    const u16 uRight = core::PackUnorm16((static_cast<f32>(PAGE_U) + GLYPH_WIDTH) / PAGE);
    const u16 vBottom = core::PackUnorm16((static_cast<f32>(PAGE_V) + GLYPH_HEIGHT) / PAGE);
    CHECK(quads[0].uv == core::PackPair(uLeft, vTop));
    CHECK(quads[1].uv == core::PackPair(uRight, vTop));
    CHECK(quads[2].uv == core::PackPair(uLeft, vBottom));
    CHECK(quads[3].uv == core::PackPair(uRight, vBottom));
    CHECK(std::ranges::all_of(quads, [](const gfx::MeshVertex& corner) { return corner.colour == LIGHT; }));
}

TEST_CASE("each glyph of a layout is a quad, in the layout's order") {
    gfx::PlacedGlyph second = GLYPH;
    second.x = SECOND_GLYPH_X;
    gfx::Layout layout = ONE_GLYPH_BOX;
    layout.glyphs = { GLYPH, second };
    std::vector<gfx::MeshVertex> quads;
    gfx::PlaceGlyphs(layout, {}, gfx::SDF_BASE_PIXELS, LIGHT, quads);
    REQUIRE(quads.size() == 2 * gfx::QUAD_VERTICES);
    CHECK(quads[0].x == GLYPH.x);
    CHECK(quads[gfx::QUAD_VERTICES].x == SECOND_GLYPH_X);
}

TEST_CASE("a text's anchor follows the render clock, offset in world units and unrounded") {
    const core::TextItem item{ .previous = FROM, .current = ONTO, .offset = ABOVE };
    // At zoom 1 on the reference size a world unit is a pixel; at zoom 2, two, from the centre.
    const core::Vec2 near = gfx::TextAnchor(item, { .alpha = ALPHA, .camera = {} }, REFERENCE_WIDTH, REFERENCE_HEIGHT);
    CHECK(near.x == doctest::Approx(BETWEEN.x));
    CHECK(near.y == doctest::Approx(BETWEEN.y));
    const core::Vec2 far =
        gfx::TextAnchor(item, { .alpha = ALPHA, .camera = { .zoom = DOUBLE } }, REFERENCE_WIDTH, REFERENCE_HEIGHT);
    constexpr core::Vec2 CENTRE = gfx::REFERENCE_CENTRE;
    CHECK(far.x == doctest::Approx(CENTRE.x + ((BETWEEN.x - CENTRE.x) * DOUBLE)));
    CHECK(far.y == doctest::Approx(CENTRE.y + ((BETWEEN.y - CENTRE.y) * DOUBLE)));
}

TEST_CASE("a label stands on its anchor and a caption hangs from it") {
    const core::Vec2 label = gfx::TextTopLeft(ANCHOR, BOX, core::TextAlign::BottomCentre);
    CHECK(label.x == BOX_LEFT);
    CHECK(label.y == BOX_TOP_STANDING);
    const core::Vec2 caption = gfx::TextTopLeft(ANCHOR, BOX, core::TextAlign::TopCentre);
    CHECK(caption.x == BOX_LEFT);
    CHECK(caption.y == ANCHOR.y);
}

TEST_CASE("with the outline and the shadow both off, labels and captions turn to dark ink; numbers keep their colour") {
    const core::TextItem label{ .kind = core::TextKind::Label, .colour = LIGHT };
    const core::TextItem caption{ .kind = core::TextKind::Caption, .colour = LIGHT };
    const core::TextItem number{ .kind = core::TextKind::Number, .colour = LIGHT };
    CHECK(gfx::TextColour(label, true, true) == LIGHT);
    CHECK(gfx::TextColour(label, false, true) == LIGHT);
    CHECK(gfx::TextColour(label, true, false) == LIGHT);
    CHECK(gfx::TextColour(label, false, false) == gfx::TEXT_INK);
    CHECK(gfx::TextColour(caption, false, false) == gfx::TEXT_INK);
    CHECK(gfx::TextColour(number, false, false) == LIGHT);
}

TEST_CASE("a text's opacity scales every channel of its premultiplied colour") {
    const core::TextItem half{ .opacity = HALF, .colour = SOME_COLOUR };
    CHECK(gfx::TextColour(half, true, true) == SOME_COLOUR_AT_HALF);
    const core::TextItem gone{ .opacity = 0.0F, .colour = SOME_COLOUR };
    CHECK(gfx::TextColour(gone, true, true) == 0U);
}

} // namespace
