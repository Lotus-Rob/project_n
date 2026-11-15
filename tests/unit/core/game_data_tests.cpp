#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/game_data.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u16 MEADOW_COST = 10;
constexpr f32 TILE = 256.0F;

// A 2 by 1 map of meadow and, when `peaks`, a second kind of tile at its right, with one city to start in.
std::shared_ptr<const core::WorldMap> Map(bool peaks) {
    core::WorldMapSource source;
    source.columns = 2;
    source.rows = 1;
    source.tileSize = TILE;
    source.kinds = { { .name = "meadow", .symbol = '.', .cost = MEADOW_COST },
                     { .name = "peaks", .symbol = '^', .cost = core::IMPASSABLE_COST } };
    source.grid = { 0, static_cast<u8>(peaks ? 1 : 0) };
    source.places = { { .key = "capital",
                        .kind = core::PlaceKind::City,
                        .sort = "",
                        .tile = { .column = 0, .row = 0 },
                        .nameKey = "place_capital",
                        .name = {},
                        .people = 1,
                        .start = true } };
    std::expected<core::WorldMap, core::Error> map = core::WorldMap::Build(std::move(source));
    REQUIRE(map.has_value());
    return std::make_shared<const core::WorldMap>(std::move(*map));
}

TEST_CASE("the game data's hash changes with the map") {
    const core::GameData none;
    core::GameData flat;
    core::GameData flatAgain;
    core::GameData rocky;
    flat.world = Map(false);
    flatAgain.world = Map(false);
    rocky.world = Map(true);
    CHECK(flat.Hash() == flatAgain.Hash());
    CHECK(flat.Hash() != rocky.Hash());
    CHECK(flat.Hash() != none.Hash());
    CHECK(none.Hash() == core::GameData{}.Hash());
}

} // namespace
