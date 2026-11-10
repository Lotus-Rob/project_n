#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/shelf_packer.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 PAGE = 32;
constexpr u32 TEXEL = 1; // the padding of these cases: one texel right and below each rectangle

TEST_CASE("glyphs of one height share a shelf, a texel apart") {
    gfx::ShelfPacker packer{ PAGE, TEXEL };
    CHECK(packer.Pack(10, 10) == gfx::PackedSlot{ .page = 0, .x = 0, .y = 0 });
    CHECK(packer.Pack(10, 9) == gfx::PackedSlot{ .page = 0, .x = 11, .y = 0 }); // 9 and 10 both round to 12
}

TEST_CASE("a taller glyph opens a shelf below") {
    gfx::ShelfPacker packer{ PAGE, TEXEL };
    CHECK(packer.Pack(20, 10) == gfx::PackedSlot{ .page = 0, .x = 0, .y = 0 });
    CHECK(packer.Pack(5, 12) == gfx::PackedSlot{ .page = 0, .x = 0, .y = 12 });
}

TEST_CASE("a glyph too wide for its shelf opens another") {
    gfx::ShelfPacker packer{ PAGE, TEXEL };
    CHECK(packer.Pack(20, 3) == gfx::PackedSlot{ .page = 0, .x = 0, .y = 0 });
    CHECK(packer.Pack(20, 3) == gfx::PackedSlot{ .page = 0, .x = 0, .y = 4 });
}

TEST_CASE("a glyph with no room on the last page starts a new one") {
    gfx::ShelfPacker packer{ PAGE, TEXEL };
    CHECK(packer.Pack(31, 31) == gfx::PackedSlot{ .page = 0, .x = 0, .y = 0 });
    CHECK(packer.Pack(31, 31) == gfx::PackedSlot{ .page = 1, .x = 0, .y = 0 });
    CHECK(packer.Pages() == 2);
}

TEST_CASE("a glyph larger than a page is refused") {
    gfx::ShelfPacker packer{ PAGE, TEXEL };
    CHECK_FALSE(packer.Pack(PAGE, 1));
    CHECK_FALSE(packer.Pack(1, PAGE));
    CHECK(packer.Pages() == 0);
}

TEST_CASE("the packer pads each glyph by the field's spread") {
    constexpr u32 LARGE_PAGE = 256;
    constexpr u32 SPREAD_PADDING = 9;
    gfx::ShelfPacker packer{ LARGE_PAGE, SPREAD_PADDING };
    CHECK(packer.Pack(10, 10) == gfx::PackedSlot{ .page = 0, .x = 0, .y = 0 });
    CHECK(packer.Pack(10, 10) == gfx::PackedSlot{ .page = 0, .x = 19, .y = 0 });
    CHECK(packer.Pack(10, 20) == gfx::PackedSlot{ .page = 0, .x = 0, .y = 20 }); // 10 + 9 rounds to 20
}

} // namespace
