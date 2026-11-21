#include "core/cxx23.hpp" // IWYU pragma: keep
#include "gfx/ground_field.hpp"
#include "support/meadow.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 COLUMNS = 6;
constexpr u32 ROWS = 4;
constexpr u8 LAKE = 1;
constexpr std::array<u8, 1> WATER{ { LAKE } };

/// A 6 x 4 map with a 2 x 2 lake at columns 3 and 4, rows 1 and 2, inside meadow.
core::WorldMap LakeMap() {
    std::array<u8, std::size_t{ COLUMNS } * ROWS> grid{};
    for (u32 row = 1; row <= 2; ++row) {
        for (u32 column = 3; column <= 4; ++column) {
            grid.at((row * COLUMNS) + column) = LAKE;
        }
    }
    return test::MapOfTiles(COLUMNS, ROWS, grid);
}

/// The shore byte of the tile at `column`, `row`.
u8 ShoreAt(const std::vector<u8>& texels, u32 column, u32 row, u32 columns = COLUMNS) {
    return texels.at((((std::size_t{ row } * columns) + column) * 2) + 1);
}

/// The kind byte of the tile at `column`, `row`.
u8 KindAt(const std::vector<u8>& texels, u32 column, u32 row, u32 columns = COLUMNS) {
    return texels.at(((std::size_t{ row } * columns) + column) * 2);
}

TEST_CASE("a shore's two sides read half a tile either way") {
    const core::WorldMap map = LakeMap();
    std::vector<u8> texels;
    gfx::GroundTexels(map, WATER, texels);
    CHECK(ShoreAt(texels, 2, 1) == 144);
    CHECK(ShoreAt(texels, 3, 1) == 112);
    CHECK(ShoreAt(texels, 5, 2) == 144);
    CHECK(ShoreAt(texels, 4, 2) == 112);
}

TEST_CASE("a land tile three centres from the water reads 2.5") {
    const core::WorldMap map = LakeMap();
    std::vector<u8> texels;
    gfx::GroundTexels(map, WATER, texels);
    CHECK(ShoreAt(texels, 0, 1) == 208);
}

TEST_CASE("a diagonal neighbour of the lake is sqrt 2 minus a half away") {
    const core::WorldMap map = LakeMap();
    std::vector<u8> texels;
    gfx::GroundTexels(map, WATER, texels);
    CHECK(ShoreAt(texels, 2, 0) == 157);
}

TEST_CASE("distances clamp at four tiles") {
    constexpr u32 LENGTH = 12;
    std::array<u8, LENGTH> grid{};
    grid.at(LENGTH - 1) = LAKE;
    const core::WorldMap map = test::MapOfTiles(LENGTH, 1, grid);
    std::vector<u8> texels;
    gfx::GroundTexels(map, WATER, texels);
    CHECK(ShoreAt(texels, 0, 0, LENGTH) == 255);
    CHECK(ShoreAt(texels, 6, 0, LENGTH) == 255);
    CHECK(ShoreAt(texels, 7, 0, LENGTH) == 240);
    CHECK(ShoreAt(texels, 10, 0, LENGTH) == 144);
}

TEST_CASE("a map all of water reads the water clamp") {
    const core::WorldMap map = test::MapOfTiles(2, 1, std::array<u8, 2>{ { 0, LAKE } });
    std::vector<u8> texels;
    gfx::GroundTexels(map, std::array<u8, 2>{ { 0, LAKE } }, texels);
    CHECK(ShoreAt(texels, 0, 0, 2) == 0);
    CHECK(ShoreAt(texels, 1, 0, 2) == 0);
}

TEST_CASE("the kind byte is the map's kind index") {
    const core::WorldMap map = LakeMap();
    std::vector<u8> texels;
    gfx::GroundTexels(map, WATER, texels);
    REQUIRE(texels.size() == std::size_t{ COLUMNS } * ROWS * 2);
    CHECK(texels.at(0) == 0);
    CHECK(KindAt(texels, 3, 1) == LAKE);
    CHECK(KindAt(texels, 4, 2) == LAKE);
    CHECK(KindAt(texels, 5, 2) == 0);
}

TEST_CASE("a map without water reads the clamp everywhere") {
    const core::WorldMap map = test::MeadowMap(COLUMNS, ROWS);
    std::vector<u8> texels;
    gfx::GroundTexels(map, WATER, texels);
    REQUIRE(texels.size() == std::size_t{ COLUMNS } * ROWS * 2);
    u8 lowest = 255;
    for (u32 row = 0; row < ROWS; ++row) {
        for (u32 column = 0; column < COLUMNS; ++column) {
            lowest = std::min(lowest, ShoreAt(texels, column, row));
        }
    }
    CHECK(lowest == 255);
}

TEST_CASE("no water kind named reads the clamp on a map with a lake") {
    const core::WorldMap map = LakeMap();
    std::vector<u8> texels;
    gfx::GroundTexels(map, {}, texels);
    CHECK(ShoreAt(texels, 3, 1) == 255);
    CHECK(KindAt(texels, 3, 1) == LAKE);
}

TEST_CASE("a shore byte rounds and clamps") {
    CHECK(gfx::ShoreByte(0.0F) == 128);
    CHECK(gfx::ShoreByte(-0.5F) == 112);
    CHECK(gfx::ShoreByte(4.0F) == 255);
    CHECK(gfx::ShoreByte(-4.0F) == 0);
    CHECK(gfx::ShoreByte(100.0F) == 255);
    CHECK(gfx::ShoreByte(-100.0F) == 0);
}

} // namespace
