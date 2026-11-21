#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/fog.hpp"

#include "support/meadow.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 SIDE = 20;
constexpr u32 RADIUS = 3;

/// The square test map: SIDE by SIDE tiles of meadow.
core::WorldMap SquareMap() {
    return test::MeadowMap(SIDE, SIDE);
}

/// How many tiles `centre` explores on a fresh bitset of the map.
u32 Explored(const core::WorldMap& map, core::TilePoint centre, u32 radius, std::vector<u64>& bits) {
    bits.assign(core::FogWords(static_cast<std::size_t>(map.Columns()) * map.Rows()), 0);
    return core::ExploreAround(bits, map, map.TileIndex(centre), radius);
}

TEST_CASE("exploring round a tile in the middle of the map marks the 25 tiles of the diamond") {
    const core::WorldMap map = SquareMap();
    std::vector<u64> bits;
    const core::TilePoint middle{ .column = 10, .row = 10 };
    CHECK(Explored(map, middle, RADIUS, bits) == 25);
    CHECK(core::ExploreAround(bits, map, map.TileIndex(middle), RADIUS) == 0);
    CHECK(core::IsExplored(bits, map.TileIndex(middle)));
    CHECK(core::IsExplored(bits, map.TileIndex({ .column = 13, .row = 10 })));
    CHECK(core::IsExplored(bits, map.TileIndex({ .column = 9, .row = 8 })));
    CHECK_FALSE(core::IsExplored(bits, map.TileIndex({ .column = 12, .row = 12 })));
    CHECK_FALSE(core::IsExplored(bits, map.TileIndex({ .column = 14, .row = 10 })));
}

TEST_CASE("exploring round the corner tile marks the 10 tiles the map holds") {
    const core::WorldMap map = SquareMap();
    std::vector<u64> bits;
    CHECK(Explored(map, { .column = 0, .row = 0 }, RADIUS, bits) == 10);
    CHECK(Explored(map, { .column = SIDE - 1, .row = SIDE - 1 }, RADIUS, bits) == 10);
}

TEST_CASE("exploring round an edge tile marks the 16 tiles the map holds") {
    const core::WorldMap map = SquareMap();
    std::vector<u64> bits;
    CHECK(Explored(map, { .column = 10, .row = 0 }, RADIUS, bits) == 16);
    CHECK(Explored(map, { .column = SIDE - 1, .row = 10 }, RADIUS, bits) == 16);
}

TEST_CASE("a radius of 0 explores the one tile") {
    const core::WorldMap map = SquareMap();
    std::vector<u64> bits;
    CHECK(Explored(map, { .column = 4, .row = 5 }, 0, bits) == 1);
    CHECK(core::IsExplored(bits, map.TileIndex({ .column = 4, .row = 5 })));
}

TEST_CASE("a tile at distance 3 is in sight and one at 4 isn't") {
    const core::WorldMap map = SquareMap();
    const u32 from = map.TileIndex({ .column = 10, .row = 10 });
    CHECK(core::InSight(map, from, map.TileIndex({ .column = 13, .row = 10 }), RADIUS));
    CHECK(core::InSight(map, from, map.TileIndex({ .column = 10, .row = 7 }), RADIUS));
    CHECK(core::InSight(map, from, map.TileIndex({ .column = 11, .row = 12 }), RADIUS));
    CHECK(core::InSight(map, from, from, RADIUS));
    CHECK_FALSE(core::InSight(map, from, map.TileIndex({ .column = 14, .row = 10 }), RADIUS));
    CHECK_FALSE(core::InSight(map, from, map.TileIndex({ .column = 12, .row = 12 }), RADIUS));
    CHECK(core::TileDistance({ .column = 2, .row = 9 }, { .column = 5, .row = 7 }) == 5);
}

TEST_CASE("fog bits fit only with the right word count and no stray bit") {
    constexpr std::size_t FULL_TILES = 12'750;
    constexpr std::size_t LAST_WORD_START = std::size_t{ 199 } * 64;
    std::vector<u64> bits(200, 0);
    CHECK(core::FogWords(FULL_TILES) == 200);
    CHECK(core::FogBitsFit(bits, FULL_TILES));
    bits[199] = u64{ 1 } << (FULL_TILES - 1 - LAST_WORD_START); // tile 12,749: the last
    CHECK(core::FogBitsFit(bits, FULL_TILES));
    bits[199] = u64{ 1 } << (FULL_TILES - LAST_WORD_START); // tile 12,750: past the map
    CHECK_FALSE(core::FogBitsFit(bits, FULL_TILES));
    bits[199] = 0;
    bits.pop_back();
    CHECK_FALSE(core::FogBitsFit(bits, FULL_TILES));
    CHECK_FALSE(core::IsExplored(bits, static_cast<u32>(FULL_TILES)));
}

} // namespace
