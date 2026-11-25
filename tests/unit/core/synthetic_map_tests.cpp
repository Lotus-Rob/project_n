#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/paths.hpp"
#include "support/synthetic_map.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 FIRST_SCALE = 1;
constexpr u32 TENTH_SCALE = 10;
constexpr u32 FIFTIETH_SCALE = 50;
constexpr u64 SEED = 1;
constexpr u32 PERCENT = 100;
constexpr u32 LEAST_CLOSED_PERCENT = 10;
constexpr u32 MOST_CLOSED_PERCENT = 30;
constexpr u32 LEAST_ROAD_TILES = 100;
constexpr core::ProfileIndex WARY = 1;

u32 CountOf(const core::WorldMap& map, core::PlaceKind kind) {
    return static_cast<u32>(std::ranges::count(map.Places(), kind, &core::Place::kind));
}

TEST_CASE("a synthetic map has its scale's size and places") {
    for (const u32 scale : { FIRST_SCALE, TENTH_SCALE }) {
        const core::WorldMap map = test::SyntheticMap(scale, SEED);
        const test::SyntheticSize size = test::SyntheticSizeOf(scale);
        CHECK(map.Columns() == size.columns);
        CHECK(map.Rows() == size.rows);
        CHECK(CountOf(map, core::PlaceKind::City) == size.cities);
        CHECK(CountOf(map, core::PlaceKind::Danger) == size.dangers);
    }
    constexpr test::SyntheticSize LARGEST = test::SyntheticSizeOf(FIFTIETH_SCALE);
    CHECK(LARGEST.columns == 1061);
    CHECK(LARGEST.rows == 601);
    CHECK(LARGEST.cities == 300);
    CHECK(LARGEST.dangers == 500);
}

TEST_CASE("a synthetic map is the same for the same seed") {
    constexpr u64 SEED_A = 7;
    constexpr u64 SEED_B = 8;
    CHECK(test::SyntheticMap(FIRST_SCALE, SEED_A).Hash() == test::SyntheticMap(FIRST_SCALE, SEED_A).Hash());
    CHECK(test::SyntheticMap(FIRST_SCALE, SEED_A).Hash() != test::SyntheticMap(FIRST_SCALE, SEED_B).Hash());
}

TEST_CASE("every synthetic city reaches the start city") {
    u32 unreached = 0;
    for (const u64 seed : { u64{ 1 }, u64{ 2 }, u64{ 3 } }) {
        const core::WorldMap map = test::SyntheticMap(FIRST_SCALE, seed);
        const u32 start = map.TileIndex(map.Places()[map.Start()].tile);
        for (const core::Place& place : map.Places()) {
            if (place.kind == core::PlaceKind::City &&
                core::FindRoute(map, core::WALKING, map.TileIndex(place.tile), start).empty()) {
                ++unreached;
            }
        }
    }
    CHECK(unreached == 0);
}

TEST_CASE("a synthetic map mixes open and closed ground") {
    const core::WorldMap map = test::SyntheticMap(FIRST_SCALE, SEED);
    const u32 tiles = map.Columns() * map.Rows();
    u32 closed = 0;
    u32 roads = 0;
    for (u32 tile = 0; tile < tiles; ++tile) {
        closed += map.CostAt(tile) == core::IMPASSABLE_COST ? 1 : 0;
        roads += (map.FlagsAt(tile) & std::to_underlying(core::TileFlag::Road)) != 0 ? 1 : 0;
    }
    CHECK(closed * PERCENT >= tiles * LEAST_CLOSED_PERCENT);
    CHECK(closed * PERCENT <= tiles * MOST_CLOSED_PERCENT);
    CHECK(roads >= LEAST_ROAD_TILES);
}

TEST_CASE("a synthetic map's profiles differ only near its dangers") {
    const core::WorldMap map = test::SyntheticMap(FIRST_SCALE, SEED);
    const u32 tiles = map.Columns() * map.Rows();
    u32 dearer = 0;
    u32 dearerAwayFromDanger = 0;
    for (u32 tile = 0; tile < tiles; ++tile) {
        if (map.CostAt(tile, WARY) != map.CostAt(tile, core::WALKING)) {
            ++dearer;
            dearerAwayFromDanger += (map.FlagsAt(tile) & std::to_underlying(core::TileFlag::Danger)) == 0 ? 1 : 0;
        }
    }
    CHECK(dearer > 0);
    CHECK(dearerAwayFromDanger == 0);
}

} // namespace
