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

TEST_CASE("the game data's hash changes with the names") {
    core::Names table;
    table.surnames = { { .characters = "王", .pinyin = "wang", .weight = 1 } };
    table.given = { { .characters = "小", .pinyin = "xiao", .weight = 1 } };
    core::Names other = table;
    other.given[0].pinyin = "xiaoo";
    const core::GameData plain;
    core::GameData named;
    core::GameData namedAgain;
    core::GameData renamed;
    named.names = std::make_shared<const core::Names>(table);
    namedAgain.names = std::make_shared<const core::Names>(table);
    renamed.names = std::make_shared<const core::Names>(other);
    CHECK(named.Hash() == namedAgain.Hash());
    CHECK(named.Hash() != renamed.Hash());
    CHECK(named.Hash() != plain.Hash());
}

TEST_CASE("the base hash follows the map and the names, and nothing else") {
    core::Names table;
    table.surnames = { { .characters = "王", .pinyin = "wang", .weight = 1 } };
    table.given = { { .characters = "小", .pinyin = "xiao", .weight = 1 } };
    core::Names other = table;
    other.given[0].pinyin = "xiaoo";
    core::GameData world;
    world.world = Map(false);
    world.names = std::make_shared<const core::Names>(table);
    core::GameData sameWorld = world; // the same map and names read again, its fights retuned
    sameWorld.world = Map(false);
    sameWorld.names = std::make_shared<const core::Names>(table);
    sameWorld.cultivator.walkSpeed = 9.0F;
    sameWorld.fox.health = 7;
    sameWorld.abilities.qiMax = 3;
    sameWorld.combat.afflictionPeriod = 11;
    core::GameData rocky = world;
    rocky.world = Map(true);
    core::GameData renamed = world;
    renamed.names = std::make_shared<const core::Names>(other);
    CHECK(sameWorld.BaseHash() == world.BaseHash());
    CHECK(sameWorld.Hash() != world.Hash()); // the data itself is other data
    CHECK(rocky.BaseHash() != world.BaseHash());
    CHECK(renamed.BaseHash() != world.BaseHash());
    CHECK(core::GameData{}.BaseHash() != world.BaseHash());
    CHECK(core::GameData{}.BaseHash() == core::GameData{}.BaseHash());
}

} // namespace
